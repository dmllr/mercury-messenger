import time
import json
import logging
import ssl
import sys
import asyncio
import websockets
from authlib.oauth2.rfc6749 import grants
from authlib.oauth2.rfc6750 import BearerTokenValidator
from authlib.common.security import generate_token

# In-memory user store
users = {
    "user1": "password1",
    "user2": "password2",
    "user3": "password3"
}


class InMemoryToken:
    def __init__(self):
        self.tokens = {}

    def generate_token(self, client, grant_type, user, scope):
        token = generate_token()
        self.tokens[token] = {
            'client_id': client.client_id if client else None,
            'user': user,
            'scope': scope,
            'expires_at': time.time() + 3600  # 1 hour expiry
        }
        return token

    def validate_token(self, token):
        token_data = self.tokens.get(token)
        if token_data and token_data['expires_at'] > time.time():
            return token_data
        return None


token_store = InMemoryToken()


class PasswordGrant(grants.ResourceOwnerPasswordCredentialsGrant):
    def authenticate_user(self, username, password):
        if users.get(username) == password:
            return username
        return None


class CustomBearerTokenValidator(BearerTokenValidator):
    def authenticate_token(self, token_string):
        return token_store.validate_token(token_string)

    def request_invalid(self, request):
        return False

    def token_revoked(self, token):
        return False


logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))

clients = {}
token_validator = CustomBearerTokenValidator()


async def handle_websocket(websocket, path):
    user_id = None
    client_id = None
    try:
        while True:
            data = await websocket.recv()
            message = json.loads(data)
            if 'type' not in message:
                print(f'Unknown message received')
                continue

            if message['type'] == 'authenticate':
                username = message.get('username')
                password = message.get('password')
                if username in users and users[username] == password:
                    token = token_store.generate_token(None, 'password', username, None)
                    user_id = username
                    print(f'User "{user_id}" authenticated')
                    response = {
                        "id": "system",
                        "type": "authenticated",
                        "token": token
                    }
                    await websocket.send(json.dumps(response))
                else:
                    response = {
                        "id": "system",
                        "type": "error",
                        "error": "invalid_credentials"
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    return

            elif message['type'] == 'publish_client':
                token = message.get('token')
                if not token or not token_validator.authenticate_token(token):
                    response = {
                        "id": "system",
                        "type": "error",
                        "error": "invalid_token"
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    return

                user_id = token_store.validate_token(token)['user']
                client_id = message.get('client_id')
                if user_id not in clients:
                    clients[user_id] = {}
                clients[user_id][client_id] = websocket
                print(f'Peer "{user_id}/{client_id}" published')
                await websocket.send(json.dumps({
                    "id": "system",
                    "type": "client_published",
                    "client_id": client_id
                }))

            else:
                token = message.get('token')
                if not token or not token_validator.authenticate_token(token):
                    response = {
                        "id": "system",
                        "type": "error",
                        "error": "invalid_token"
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    if user_id in clients and client_id in clients[user_id]:
                        del clients[user_id][client_id]
                        print(f'Peer "{user_id}/{client_id}" disconnected due to invalid token')
                    return

                user_id = token_store.validate_token(token)['user']
                destination_id = message['id']
                if destination_id in clients[user_id]:
                    client = clients[user_id][destination_id]
                    message['id'] = client_id
                    data = json.dumps(message)
                    print(f'"{destination_id}" >> {client_id}:\n{data}')
                    await client.send(data)
                else:
                    print(f'Peer "{destination_id}" not published by {user_id} or unauthorized')
                    response = {
                        "id": "system",
                        "type": "error",
                        "error": "peer_not_found"
                    }
                    await websocket.send(json.dumps(response))

    except Exception as e:
        print(e)

    finally:
        if user_id and client_id and user_id in clients and client_id in clients[user_id]:
            del clients[user_id][client_id]
            print(f'Peer "{user_id}/{client_id}" disconnected')


if __name__ == '__main__':
    # Usage: ./signaling-server.py [[host:]port] [SSL certificate file]
    endpoint_or_port = sys.argv[1] if len(sys.argv) > 1 else "8000"
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None
    ssl_key = sys.argv[3] if len(sys.argv) > 3 else None

    endpoint = endpoint_or_port if ':' in endpoint_or_port else "0.0.0.0:" + endpoint_or_port

    if ssl_cert:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert, keyfile=ssl_key)
    else:
        ssl_context = None

    print(f'Listening on {endpoint}')
    host, port = endpoint.rsplit(':', 1)
    start_server = websockets.serve(handle_websocket, host, int(port), ssl=ssl_context)
    asyncio.get_event_loop().run_until_complete(start_server)
    asyncio.get_event_loop().run_forever()
