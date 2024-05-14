#!/usr/bin/env python

import asyncio
import json
import logging
import ssl
import sys
import websockets
from authlib.oauth2.rfc6750 import BearerTokenValidator
from auth_server import token_store  # Ensure this is the correct import

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))

clients = {}
users = {}

class CustomBearerTokenValidator(BearerTokenValidator):
    def authenticate_token(self, token_string):
        return token_store.validate_token(token_string)

    def request_invalid(self, request):
        return False

    def token_revoked(self, token):
        return False

token_validator = CustomBearerTokenValidator()

async def handle_websocket(websocket, path):
    token = None
    user_id = None
    try:
        # Extract token from the query parameter (e.g., ws://localhost:8000/?token=token1)
        query = path.split('?')[1] if '?' in path else ''
        params = dict(qc.split('=') for qc in query.split('&'))
        token = params.get('token')

        if not token or not token_validator.authenticate_token(token):
            print('Invalid or missing token. Closing connection.')
            await websocket.close()
            return

        user_id = token_store.validate_token(token)['user']
        print('User "{}" connected'.format(user_id))

        clients[user_id] = websocket
        while True:
            data = await websocket.recv()
            print('"{}" << {}'.format(user_id, data))
            message = json.loads(data)
            destination_id = message['id']

            # Only send message if destination user is the same
            if destination_id in clients and clients[destination_id] == websocket:
                destination_websocket = clients[destination_id]
                message['id'] = user_id
                data = json.dumps(message)
                print('"{}" >> {}'.format(destination_id, data))
                await destination_websocket.send(data)
            else:
                print('Peer "{}" not found or unauthorized'.format(destination_id))

    except Exception as e:
        print(e)

    finally:
        if user_id:
            del clients[user_id]
            print('User "{}" disconnected'.format(user_id))

if __name__ == '__main__':
    # Load users only if needed; now handled by auth_server.py

    # Usage: ./server.py [[host:]port] [SSL certificate file]
    endpoint_or_port = sys.argv[1] if len(sys.argv) > 1 else "8000"
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None
    ssl_key = sys.argv[3] if len(sys.argv) > 3 else None

    endpoint = endpoint_or_port if ':' in endpoint_or_port else "0.0.0.0:" + endpoint_or_port

    if ssl_cert:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert, keyfile=ssl_key)
    else:
        ssl_context = None

    print('Listening on {}'.format(endpoint))
    host, port = endpoint.rsplit(':', 1)
    start_server = websockets.serve(handle_websocket, host, int(port), ssl=ssl_context)
    asyncio.get_event_loop().run_until_complete(start_server)
    asyncio.get_event_loop().run_forever()
