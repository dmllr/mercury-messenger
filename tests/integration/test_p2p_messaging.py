import pytest
from utils.framework import Scenario

def test_p2p_messaging_same_network(test_environment):
  """
  Scenario: same network simple communication:
  1. Signaling Server on Network 10.0/24.
  2. Mock-App A on Network 10.0/24.
  3. Mock-App B on Network 10.0/24.
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
    executable="/app/bin/mercury-bootstrap",
    args=["--port", "8000"],
    networks={"net-main": signaling_ip}
  )

  # App A
  script_a = (
    f"connect, wait 1, "
    f"message {id_b} \"Hello B from A\", "
    f"wait 15, disconnect"
  )
  scenario.add_process(
    name="mercury-mock-app-a",
    executable="/app/bin/mercury-mock-app",
    args=[
      "--key", key_a,
      "--signaling-url", f"/ip4/{signaling_ip}/tcp/8000",
      "--db-path", "/tmp/a.db",
      "--script", script_a
    ],
    networks={"net-main": "192.168.10.31"}
  )

  # App B
  script_b = f"connect, wait 2, confirm {id_a}, wait 15, disconnect"
  scenario.add_process(
    name="mercury-mock-app-b",
    executable="/app/bin/mercury-mock-app",
    args=[
      "--key", key_b,
      "--signaling-url", f"/ip4/{signaling_ip}/tcp/8000",
      "--db-path", "/tmp/b.db",
      "--script", script_b
    ],
    networks={"net-main": "192.168.10.32"}
  )

  # Expectations
  # Signaling Server should see authentication
  scenario.expect_logs("signaling", [
    "Bootstrap Node Active. Multiaddr:"
  ])

  # App A should confirm sending
  scenario.expect_logs("mercury-mock-app-a", [
    f"[mock] Message to {id_b} sent",
    f"[mock] Message to {id_b} delivered",
  ])

  # App B should receive message
  scenario.expect_logs("mercury-mock-app-b", [
    f"[mock] Message from {id_a} received: Hello B from A",
  ])

  # Execute
  test_environment.execute_scenario(scenario, timeout_seconds=20)
