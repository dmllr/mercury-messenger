import json
import ssl
import sys
import asyncio
from typing import Union
import logging
from enum import Enum

import websockets

from auth_manager import AuthManager

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logging.getLogger("websockets").addHandler(logging.NullHandler())


class Peer:
    class Kind(str, Enum):
        Streamer = 'streamer'
        Viewer = 'viewer'

    def __init__(self, id, name, kind, ws):
        self.id: str = id
        self.name: str = name
        self.kind: Peer.Kind = kind
        self.ws: websockets.WebSocketServerProtocol = ws


class Streamer(Peer):
    def __init__(self, id, name, kind, ws):
        super().__init__(id, name, kind, ws)
        self.profile = {}


class Viewer(Peer):
    def __init__(self, id, name, kind, ws):
        super().__init__(id, name, kind, ws)


# Mapping: user_id -> (peer_id -> Peer instance)
peers: dict[str, dict[str, Union[Streamer, Viewer]]] = {}
authManager = AuthManager()


async def handle_connection(websocket: websockets.WebSocketServerProtocol):
    """Handles a new WebSocket connection"""
    user_id = None
    peer_id = None

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

    async def send_streaming_peers_to_viewer(connection):
        """Sends the list of streamers to a peer"""
        jdump = json.dumps({
            "type": "info streamers",
            "peers": [
                {"id": cid, "name": c.name, "profile": c.profile}
                for cid, c in peers[user_id].items() if c.kind == Peer.Kind.Streamer
            ]
        })
        logging.debug(f'>>[{user_id}/{peer_id}]: {jdump}')
        await connection.send(jdump)

    async def send_streaming_peers_to_all_viewers():
        """Sends the updated list of streamers to all viewers"""
        for peer in peers.get(user_id, {}).values():
            if peer.kind == Peer.Kind.Viewer:
                await send_streaming_peers_to_viewer(peer.ws)

    async def notify_peer_disconnected(disconnected_peer: Peer):
        """
        Notifies opposite type peers that a peer has disconnected.
        For example, if a streamer disconnects, notify viewers; if a viewer disconnects, notify streamers.
        """
        target_kind = Peer.Kind.Viewer if disconnected_peer.kind == Peer.Kind.Streamer else Peer.Kind.Streamer
        notification = json.dumps({
            "type": "peer disconnected",
            "id": disconnected_peer.id,
        })
        for peer in peers.get(user_id, {}).values():
            if peer.kind == target_kind:
                try:
                    logging.debug(f'Notifying {user_id}/{peer.id} of disconnection of {disconnected_peer.id}')
                    await peer.ws.send(notification)
                except Exception:
                    logging.error(f"Error notifying peer {user_id}/{peer.id} about disconnection of {disconnected_peer.id}", exc_info=True)

    try:
        async for data in websocket:
            logging.debug(f'<<[{user_id}/{peer_id}] {data}')

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
                    peer_id = message.get('peer_id')
                    peer_name = message.get('name')
                    peer_kind = message.get('kind')

                    user_peers = peers.setdefault(user_id, {})
                    existing_peer = user_peers.get(peer_id)
                    if existing_peer:
                        if existing_peer.ws != websocket:
                            logging.info(f'Replacing existing connection for peer "{user_id}/{peer_id}"')
                            try:
                                await existing_peer.ws.close(code=4001, reason="Replaced by new connection")
                            except Exception:
                                logging.exception("Error closing old connection")
                            await notify_peer_disconnected(existing_peer)
                            # Create a new peer instance for the new connection.
                            peer_class = Viewer if peer_kind == Peer.Kind.Viewer else Streamer
                            peer_instance = peer_class(peer_id, peer_name, peer_kind, websocket)
                            if peer_kind == Peer.Kind.Streamer:
                                peer_instance.profile = message.get('profile')
                            user_peers[peer_id] = peer_instance
                        else:
                            # Same connection: update the peer's information.
                            logging.info(f'Updating peer "{user_id}/{peer_id}" on the same connection')
                            existing_peer.name = peer_name
                            existing_peer.kind = peer_kind
                            if peer_kind == Peer.Kind.Streamer:
                                existing_peer.profile = message.get('profile')
                    else:
                        peer_class = Viewer if peer_kind == Peer.Kind.Viewer else Streamer
                        peer_instance = peer_class(peer_id, peer_name, peer_kind, websocket)
                        if peer_kind == Peer.Kind.Streamer:
                            peer_instance.profile = message.get('profile')
                        user_peers[peer_id] = peer_instance

                    logging.info(f'Peer "{user_id}/{peer_id}" published')
                    await send_streaming_peers_to_all_viewers()

                elif message['type'] == 'get streamers':
                    await send_streaming_peers_to_viewer(websocket)

                else:
                    destination_id = message.get('to')
                    if destination_id in peers[user_id]:
                        peer = peers[user_id][destination_id]
                        message['from'] = peer_id
                        data_to_send = json.dumps(message)
                        logging.debug(f'>>[{user_id}/{peer_id}] to "{user_id}/{destination_id}": {data_to_send}')
                        await peer.ws.send(data_to_send)
                    else:
                        logging.warning(f'Peer "{destination_id}" not found')
                        response = {
                            "type": "error",
                            "error": "peer_not_found",
                            "message": f'Peer "{user_id}/{destination_id}" not found'
                        }
                        await websocket.send(json.dumps(response))

    except (asyncio.CancelledError, websockets.ConnectionClosedError, websockets.ConnectionClosedOK):
        logging.info(f"Connection closed for peer {user_id}/{peer_id}")
    except Exception:
        logging.error(f"Unexpected error for peer {user_id}/{peer_id}", exc_info=True)
    finally:
        # On disconnect, remove the peer and notify opposite peers.
        if user_id and peer_id and user_id in peers and peer_id in peers[user_id]:
            disconnected_peer = peers[user_id][peer_id]
            del peers[user_id][peer_id]
            logging.info(f'Peer "{user_id}/{peer_id}" disconnected')
            try:
                await send_streaming_peers_to_all_viewers()
            except Exception:
                logging.error("Error while sending the streamer list", exc_info=True)
            await notify_peer_disconnected(disconnected_peer)


async def main():
    """Starts the WebSocket server"""
    async with websockets.serve(handle_connection, host, int(port), ssl=ssl_context):
        await asyncio.Future()  # Keep the server running indefinitely


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

    asyncio.run(main())
