import asyncio
import websockets
import json

async def authenticate_and_publish_client():
    uri = "ws://localhost:8000"

    async with websockets.connect(uri) as websocket:
        # Authenticate and obtain a token
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
        await websocket.send(json.dumps({"type": "publish_client", "token": token, "client_id": "client1"}))
        response = await websocket.recv()
        response_data = json.loads(response)
        if response_data.get("type") == "client_published":
            print("Client published:", response_data["client_id"])
        else:
            print("Failed to publish client:", response_data)
            return

        # Send a message to another client of the same user
        await websocket.send(json.dumps({"type": "message", "token": token, "id": "client2", "content": "Hello, client2!"}))
        response = await websocket.recv()
        print("Message response:", response)  # Response from client2 or server acknowledgment

asyncio.run(authenticate_and_publish_client())
