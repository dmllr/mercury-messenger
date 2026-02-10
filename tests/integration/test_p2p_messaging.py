import pytest
from utils.framework import Scenario

def test_p2p_messaging_multi_network(test_environment):
  """
  Scenario:
  1. Signaling Server on Network A.
  2. Mock-App A on Network A.
  3. Mock-App B on Network A (simulating same LAN).
  4. Two independent identities.
  """

  # Generate Identities
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  print(f"Generated A: {id_a}")
  print(f"Generated B: {id_b}")

  # Infrastructure
  signaling_ip = "192.168.10.30"

  scenario = Scenario()

  # Define Network
  scenario.add_network("net-main", "192.168.10.0/24", "192.168.10.1")

  # Signaling Server
  scenario.add_process(
    name="signaling",
    executable="/app/bin/mercury-server",
    args=["--port", "8000"],
    networks={"net-main": signaling_ip}
  )

  # App A
  script_a = (
    f"connect, wait 1, "
    f"message {id_b} \"Hello B from A\", "
    f"wait 5, disconnect"
  )
  scenario.add_process(
    name="mercury-mock-app-a",
    executable="/app/bin/mercury-mock-app",
    args=[
      "--key", key_a,
      "--signaling-url", f"ws://{signaling_ip}:8000",
      "--db-path", "/tmp/a.db",
      "--script", script_a
    ],
    networks={"net-main": "192.168.10.31"}
  )

  # App B
  script_b = "connect, wait 6, disconnect"
  scenario.add_process(
    name="mercury-mock-app-b",
    executable="/app/bin/mercury-mock-app",
    args=[
      "--key", key_b,
      "--signaling-url", f"ws://{signaling_ip}:8000",
      "--db-path", "/tmp/b.db",
      "--script", script_b
    ],
    networks={"net-main": "192.168.10.32"}
  )

  # Expectations
  # Signaling Server should see authentication
  scenario.expect_logs("signaling", [
    f"Peer authenticated: {id_a}",
    f"Peer authenticated: {id_b}"
  ])

  # App A should confirm sending
  scenario.expect_logs("mercury-mock-app-a", [
    "WebSocket connected, signaling ready",
    f"[mock] Message to {id_b} sent",
    f"[mock] Message to {id_b} delivered",
  ])

  # App B should receive message
  scenario.expect_logs("mercury-mock-app-b", [
    "WebSocket connected, signaling ready",
    f"[mock] Message from {id_a} received: Hello B from A",
  ])

  # Execute
  test_environment.execute_scenario(scenario, timeout_seconds=15)
