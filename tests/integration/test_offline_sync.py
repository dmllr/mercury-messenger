import pytest
from utils.framework import Scenario

def test_offline_message_sync(test_environment):
    """
    Test that messages sent while peers are offline are queued correctly,
    timestamped, and synced in the exact correct order once both connect.
    """
    id_a, key_a = test_environment.gen_identity()
    id_b, key_b = test_environment.gen_identity()

    scenario = Scenario()
    scenario.add_network("net", "192.168.45.0/24", "192.168.45.1")
    scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.45.30"})

    # A script:
    # T=0: Sends A1, A2
    # T=2: Sends A3, A4
    # T=4: Sends A5, then connects
    script_a = (
        f"message {id_b} \"A1\", message {id_b} \"A2\", "
        f"wait 2, "
        f"message {id_b} \"A3\", message {id_b} \"A4\", "
        f"wait 2, "
        f"message {id_b} \"A5\", "
        f"connect, wait 15, disconnect"
    )
    scenario.add_process(
        "app-a", "/app/bin/mercury-mock-app",
        ["--key", key_a, "--signaling-url", "/ip4/192.168.45.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
        {"net": "192.168.45.31"}
    )

    # B script:
    # T=1: Sends B1
    # T=3: Sends B2, B3, B4
    # T=4: Connects
    script_b = (
        f"wait 1, message {id_a} \"B1\", "
        f"wait 2, message {id_a} \"B2\", message {id_a} \"B3\", message {id_a} \"B4\", "
        f"wait 1, connect, wait 15, disconnect"
    )
    scenario.add_process(
        "app-b", "/app/bin/mercury-mock-app",
        ["--key", key_b, "--signaling-url", "/ip4/192.168.45.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
        {"net": "192.168.45.32"}
    )

    # The expect_logs method inherently validates the strict chronological order of occurrences in the output.
    scenario.expect_logs("app-b", [
        f"[mock] Message from {id_a} received: A{i}" for i in range(1, 6)
    ])
    scenario.expect_logs("app-a", [
        f"[mock] Message from {id_b} received: B{i}" for i in range(1, 5)
    ])

    test_environment.execute_scenario(scenario, timeout_seconds=25)
