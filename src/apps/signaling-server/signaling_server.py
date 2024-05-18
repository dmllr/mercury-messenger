import json
import ssl
import sys
import asyncio
from typing import Union

import websockets
import traceback
from enum import Enum

from websockets import WebSocketServerProtocol

from auth_manager import AuthManager


class Client:
    class Kind(str, Enum):
        Producer = 'producer',
        Consumer = 'consumer'

    def __init__(self, id, kind, ws):
        self.id: str = id
        self.kind: Client.Kind = kind
        self.ws: WebSocketServerProtocol = ws


clients: dict[str, dict[str, Client]] = {}
authManager = AuthManager()


async def handle_connection(websocket: WebSocketServerProtocol):
    user_id = None
    client_id = None

    async def authenticate():
        username = message.get('username')
        password = message.get('password')
        if not authManager.authorize(username, password):
            await websocket.send(json.dumps({
                "type": "error",
                "error": "invalid_credentials"
            }))
            await websocket.close()
            return False

        token = authManager.generate_token('password', username, None)
        print(f'User "{username}" authenticated')
        await websocket.send(json.dumps({
            "type": "authenticated",
            "token": token
        }))
        return True

    async def validate_token() -> Union[str, bool]:
        token = message.get('token')
        if not token or not authManager.authenticate_token(token):
            if user_id in clients and client_id in clients[user_id]:
                del clients[user_id][client_id]
                print(f'Peer "{user_id}/{client_id}" disconnected due to invalid token')
            error = {
                "type": "error",
                "error": "invalid_token",
                "message": f'Token "{token}" failed validation'
            }
            await websocket.send(json.dumps(error))
            await websocket.close()
            return False
        return authManager.validate_token(token)['user']

    async def send_clients(connection):
        data = json.dumps({
            "type": "list_of_clients",
            "clients": [cid for cid, c in clients[user_id].items() if c.kind == Client.Kind.Producer]
        })
        print(f'>>[{user_id}/{client_id}]: {data}')
        await connection.send(data)

    # noinspection PyBroadException
    try:
        while True:
            data = await websocket.recv()
            print(f'<<[{user_id}/{client_id}] {data}')

            message = json.loads(data)
            if 'type' not in message:
                print(f'Unknown message received!')
                print(data)
                continue

            if message['type'] == 'authenticate':
                if not await authenticate():
                    return
            else:
                user_id = await validate_token()
                if not user_id:
                    return

                if message['type'] == 'exhibit':
                    client_id = message.get('client_id')
                    client_kind = message.get('kind')
                    if user_id not in clients:
                        clients[user_id] = {}
                    clients[user_id][client_id] = Client(client_id, client_kind, websocket)
                    print(f'Peer "{user_id}/{client_id}" published')
                    for client in clients[user_id]:
                        await send_clients(clients[user_id][client].ws)

                elif message['type'] == 'get clients':
                    await send_clients(websocket)

                else:
                    destination_id = message['to']
                    if destination_id in clients[user_id]:
                        # redirect the message to a destination client
                        client = clients[user_id][destination_id]
                        message['from'] = client_id
                        data = json.dumps(message)
                        print(f'>>[{user_id}/{client_id}] from "{user_id}/{client_id}": {data}')
                        await client.ws.send(data)
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
    # Usage: ./signaling_server.py [[host:]port] [SSL certificate file]
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
