import pytest
from utils.framework import Scenario

def test_replies(test_environment):
    id_a, key_a = test_environment.gen_identity()
    id_b, key_b = test_environment.gen_identity()

    scenario = Scenario()
    scenario.add_network("net", "192.168.95.0/24", "192.168.95.1")
    scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.95.30"})

    # A sends a message.
    script_a = (
        f"connect, wait 2, message {id_b} \"Hello B\", "
        f"wait 10, disconnect"
    )
    scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                         ["--key", key_a, "--signaling-url", "ws://192.168.95.30:8000", "--db-path", "/tmp/a.db", "--script", script_a],
                         {"net": "192.168.95.31"})

    # B connects, waits for A's message, replies to it.
    script_b = (
        f"connect, wait 5, reply {id_a} \"Replying to Hello B\", "
        f"wait 5, disconnect"
    )
    scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                         ["--key", key_b, "--signaling-url", "ws://192.168.95.30:8000", "--db-path", "/tmp/b.db", "--script", script_b],
                         {"net": "192.168.95.32"})

    scenario.expect_logs("app-a", ["[mock] Message from " + id_b + " received: Replying to Hello B", "[mock] Message is a reply to:"])

