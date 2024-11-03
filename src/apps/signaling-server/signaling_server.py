import json
import ssl
import sys
import asyncio
from typing import Union
import logging
from enum import Enum

import websockets
from websockets import WebSocketServerProtocol

from auth_manager import AuthManager


# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logging.getLogger("websockets").addHandler(logging.NullHandler())


class Client:
    class Kind(str, Enum):
        Producer = 'producer',
        Consumer = 'consumer'

    def __init__(self, id, name, kind, ws):
        self.id: str = id
        self.name: str = name
        self.kind: Client.Kind = kind
        self.ws: WebSocketServerProtocol = ws


class Producer(Client):
    def __init__(self, id, name, kind, ws):
        super().__init__(id, name, kind, ws)
        self.profile = {}


class Consumer(Client):
    def __init__(self, id, name, kind, ws):
        super().__init__(id, name, kind, ws)


clients: dict[str, dict[str, Union[Producer, Consumer]]] = {}
authManager = AuthManager()


async def check_client_liveliness():
    while True:
        disconnected_clients = []
        for user_id, user_clients in clients.items():
            for client_id, client in list(user_clients.items()):
                if client.ws.closed:
                    disconnected_clients.append((user_id, client_id))

        for user_id, client_id in disconnected_clients:
            del clients[user_id][client_id]
            logging.info(f'Removed disconnected client "{user_id}/{client_id}"')
            if not clients[user_id]:  # If no more clients for this user, delete user entry
                del clients[user_id]
        await asyncio.sleep(10)  # Run every 10 seconds


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
        logging.info(f'User "{username}" authenticated')
        await websocket.send(json.dumps({
            "type": "authenticated",
            "token": token
        }))
        return True

    async def validate_token() -> Union[str, bool]:
        token = message.get('token')
        if not token or not authManager.authenticate_token(token):
            error = {
                "type": "error",
                "error": "invalid_token",
                "message": f'Token "{token}" failed validation'
            }
            await websocket.send(json.dumps(error))
            await websocket.close()
            return False
        return authManager.validate_token(token)['user']

    async def send_publishers_for_client(connection):
        jdump = json.dumps({
            "type": "info publishers",
            "clients": [{"id": cid, "name": c.name, "profile": c.profile} for cid, c in clients[user_id].items() if c.kind == Client.Kind.Producer]
        })
        logging.debug(f'>>[{user_id}/{client_id}]: {jdump}')
        await connection.send(jdump)

    async def send_publishers():
        for client in clients[user_id]:
            await send_publishers_for_client(clients[user_id][client].ws)

    try:
        while True:
            data = await websocket.recv()
            logging.debug(f'<<[{user_id}/{client_id}] {data}')

            message = json.loads(data)
            if 'type' not in message:
                logging.warning(f'Unknown message received: {data}')
                continue

            if message['type'] == 'authenticate':
                if not await authenticate():
                    return
            else:
                user_id = await validate_token()
                if not user_id:
                    return

                if message['type'] == 'publish':
                    client_id = message.get('client_id')
                    client_name = message.get('name')
                    client_kind = message.get('kind')
                    if user_id not in clients:
                        clients[user_id] = {}
                    clients[user_id][client_id] = (
                        (Consumer if client_kind == Client.Kind.Consumer else Producer)(
                            client_id, client_name, client_kind, websocket))
                    if client_kind == Client.Kind.Producer:
                        clients[user_id][client_id].profile = message.get('profile')
                    logging.info(f'Peer "{user_id}/{client_id}" published')
                    await send_publishers()

                elif message['type'] == 'get publishers':
                    await send_publishers_for_client(websocket)

                else:
                    destination_id = message['to']
                    if destination_id in clients[user_id]:
                        # Redirect the message to a destination client
                        client = clients[user_id][destination_id]
                        message['from'] = client_id
                        data = json.dumps(message)
                        logging.debug(f'>>[{user_id}/{client_id}] from "{user_id}/{client_id}": {data}')
                        await client.ws.send(data)
                    else:
                        logging.warning(f'Peer "{destination_id}" not published by {user_id} or unauthorized')
                        response = {
                            "type": "error",
                            "error": "peer_not_found",
                            "message": f'Peer "{user_id}/{destination_id}" not published'
                        }
                        await websocket.send(json.dumps(response))

    except websockets.exceptions.ConnectionClosedError as e:
        logging.error(f"Connection closed with error for client {user_id}/{client_id}: {e}", exc_info=True)
    except websockets.exceptions.ConnectionClosedOK:
        logging.info(f"Connection closed gracefully for client {user_id}/{client_id}")
    except Exception as e:
        logging.error(f"Unexpected error for client {user_id}/{client_id}: {e}", exc_info=True)
    finally:
        if user_id and client_id and user_id in clients and client_id in clients[user_id]:
            del clients[user_id][client_id]
            logging.info(f'Peer "{user_id}/{client_id}" disconnected')
            try:
                await send_publishers()
            except Exception as e:
                logging.error("Error while sending publishers", exc_info=True)


if __name__ == '__main__':
    endpoint_or_port = sys.argv[1] if len(sys.argv) > 1 else "8000"
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None
    ssl_key = sys.argv[3] if len(sys.argv) > 3 else None

    endpoint = endpoint_or_port if ':' in endpoint_or_port else "0.0.0.0:" + endpoint_or_port

    if ssl_cert:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert, keyfile=ssl_key)
    else:
        ssl_context = None

    logging.info(f'Listening on {endpoint}')
    host, port = endpoint.rsplit(':', 1)
    start_server = websockets.serve(handle_connection, host, int(port), ssl=ssl_context)

    loop = asyncio.get_event_loop()
    loop.create_task(check_client_liveliness())
    loop.run_until_complete(start_server)
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    except Exception as e:
        logging.error("Server error", exc_info=True)
