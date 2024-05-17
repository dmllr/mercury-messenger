import asyncio
import websockets
import json


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

        # Publish a client
        await websocket.send(json.dumps({"type": "exhibit", "token": token, "client_id": "client1"}))

        print("Send a message to the same client of the same user")
        await websocket.send(
            json.dumps({"type": "message", "token": token, "to": "client1", "content": "Hello, myself!"}))
        response = await websocket.recv()
        print("Message response:", response)

        # Get all published clients
        print("Send 'get clients'")
        await websocket.send(
            json.dumps({"type": "get clients", "token": token}))
        response = await websocket.recv()
        print("Message response:", response)


asyncio.run(authenticate_and_exhibit())
