import json
import ssl
import sys
import asyncio
import websockets
import traceback
from auth import AuthManager

clients = {}
authManager = AuthManager()

async def handle_connection(websocket, path):
    user_id = None
    client_id = None
    # noinspection PyBroadException
    try:
        while True:
            data = await websocket.recv()
            print(f'<< {data}')

            message = json.loads(data)
            if 'type' not in message:
                print(f'Unknown message received!')
                print(data)
                continue

            if message['type'] == 'authenticate':
                username = message.get('username')
                password = message.get('password')
                if authManager.authorize(username, password):
                    token = authManager.generate_token(None, 'password', username, None)
                    user_id = username
                    print(f'User "{user_id}" authenticated')
                    response = {
                        "type": "authenticated",
                        "token": token
                    }
                    await websocket.send(json.dumps(response))
                else:
                    response = {
                        "type": "error",
                        "error": "invalid_credentials"
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    return

            elif message['type'] == 'exhibit':
                token = message.get('token')
                if not token or not authManager.authenticate_token(token):
                    response = {
                        "type": "error",
                        "error": "invalid_token",
                        "message": f'Token "{token}" failed validation'
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    return

                user_id = authManager.validate_token(token)['user']
                client_id = message.get('client_id')
                if user_id not in clients:
                    clients[user_id] = {}
                clients[user_id][client_id] = websocket
                print(f'Peer "{user_id}/{client_id}" published')

            else:
                token = message.get('token')
                if not token or not authManager.authenticate_token(token):
                    response = {
                        "type": "error",
                        "error": "invalid_token",
                        "message": f'Token "{token}" failed validation'
                    }
                    await websocket.send(json.dumps(response))
                    await websocket.close()
                    if user_id in clients and client_id in clients[user_id]:
                        del clients[user_id][client_id]
                        print(f'Peer "{user_id}/{client_id}" disconnected due to invalid token')
                    return

                user_id = authManager.validate_token(token)['user']
                destination_id = message['to']
                if destination_id in clients[user_id]:
                    # redirect the message to a destination client
                    client = clients[user_id][destination_id]
                    message['from'] = client_id
                    data = json.dumps(message)
                    print(f'to: "{user_id}/{destination_id}" from "{user_id}/{client_id}": {data}')
                    await client.send(data)
                else:
                    print(f'Peer "{destination_id}" not published by {user_id} or unauthorized')
                    response = {
                        "type": "error",
                        "error": "peer_not_found",
                        "message": f'Peer "{user_id}/{destination_id}" not published'
                    }
                    await websocket.send(json.dumps(response))

    except KeyboardInterrupt:
        ...
    except websockets.exceptions.ConnectionClosedError:
        ...
    except websockets.exceptions.ConnectionClosedOK:
        ...
    except Exception:
        print(traceback.format_exc())

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
    start_server = websockets.serve(handle_connection, host, int(port), ssl=ssl_context)
    asyncio.get_event_loop().run_until_complete(start_server)
    # noinspection PyBroadException
    try:
        asyncio.get_event_loop().run_forever()
    except KeyboardInterrupt:
        ...
    except Exception:
        print(traceback.format_exc())
