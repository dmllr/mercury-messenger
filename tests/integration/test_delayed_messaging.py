import pytest
from utils.framework import Scenario

def test_delayed_receiver_start(test_environment):
  """
  Sender sends message, Receiver starts later.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.50.0/24", "192.168.50.1")

  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.50.30"})

  # App A sends message immediately then waits
  # We need to make sure A doesn't disconnect before B comes online if we want P2P delivery.
  # Or does the server store it? No, Mercury is P2P.
  # So A must stay online and retry delivery.
  # DeliveryService has retry logic.
  
  script_a = f"connect, wait 15, message {id_b} \"Delayed Msg\", wait 65, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", 
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.50.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.50.31"})

  # App B starts after 5 seconds
  # We simulate delayed start by a long initial wait in script before 'connect'?
  # Or physically starting container later?
  # The mock app script runs sequentially.
  script_b = f"stop, wait 10, start, wait 15, confirm {id_a}, wait 45, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", 
                       ["--key", key_b, "--signaling-url", "/ip4/192.168.50.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.50.32"})

  scenario.expect_logs("app-b", [f"[mock] Message from {id_a} received: Delayed Msg"])
  scenario.expect_logs("app-a", [f"[mock] Message to {id_b} delivered"])

  test_environment.execute_scenario(scenario, timeout_seconds=60, stop_on_first_exit=False)
