import asyncio
import websockets
import json
import time


async def authenticate_and_exhibit():
    uri = "ws://localhost:8000"

    async with websockets.connect(uri) as websocket:
        print("Authenticate and obtain a token")
        await websocket.send(json.dumps({"type": "authenticate", "username": "user1", "password": "password1"}))
        response = await websocket.recv()
        response_data = json.loads(response)
        if response_data.get("type") == "authenticated":
            token = response_data["token"]
            print("Authenticated with token:", token)
        else:
            print("Failed to authenticate:", response_data)
            return

        print("Publish a client")
        await websocket.send(json.dumps({"type": "publish", "token": token, "client_id": "client1", "name": "Nice name", "kind": "streamer",
                                         "profile": [{"id": "0", "name": "Back",
                                                      "resolutions": [{"height": 480, "width": 640},
                                                                      {"height": 144, "width": 176},
                                                                      {"height": 960, "width": 1280}]},
                                                     {"id": "1", "name": "Front",
                                                      "resolutions": [{"height": 1440, "width": 1920},
                                                                      {"height": 1080, "width": 1920},
                                                                      {"height": 600, "width": 800},
                                                                      {"height": 240, "width": 320}]}]}))
        response = await websocket.recv()
        print("Message response:", response)

        print("Rename a client")
        await websocket.send(json.dumps({"type": "publish", "token": token, "client_id": "client1", "name": "Brand new name", "kind": "streamer",
                                         "profile": [{"id": "0", "name": "Back",
                                                      "resolutions": [{"height": 480, "width": 640},
                                                                      {"height": 144, "width": 176},
                                                                      {"height": 960, "width": 1280}]},
                                                     {"id": "1", "name": "Front",
                                                      "resolutions": [{"height": 1440, "width": 1920},
                                                                      {"height": 1080, "width": 1920},
                                                                      {"height": 600, "width": 800},
                                                                      {"height": 240, "width": 320}]}]}))
        response = await websocket.recv()
        print("Message response:", response)

        print("Send a message to the same client of the same user")
        await websocket.send(
            json.dumps({"type": "message", "token": token, "to": "client1", "content": "Hello, myself!"}))
        response = await websocket.recv()
        print("Message response:", response)

        # Get all published clients
        print("Send 'get streamers'")
        await websocket.send(
            json.dumps({"type": "get streamers", "token": token}))
        response = await websocket.recv()
        print("Message response:", response)

        time.sleep(1)

        print("Republish a client as viewer")
        await websocket.send(
            json.dumps({"type": "publish", "token": token, "client_id": "client1", "kind": "viewer"}))
        response = await websocket.recv()
        print("Message response:", response)


asyncio.run(authenticate_and_exhibit())
