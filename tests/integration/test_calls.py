import pytest
from utils.framework import Scenario

def test_calls_reject(test_environment):
  """
  Call -> Reject by Caller
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.90.0/24", "192.168.90.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.90.30"})

  script_a = f"connect, wait 5, message {id_b} \"init\", wait 15, call {id_b}, wait 20, hangup {id_b}, wait 20, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "/ip4/192.168.90.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.90.31"})

  script_b = f"connect, wait 70, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "/ip4/192.168.90.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.90.32"})

  scenario.expect_logs("app-b", [f"[mock] Incoming call from {id_a}", f"[mock] Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=85, stop_on_first_exit=False)

def test_call_reject_by_responder(test_environment):
  """
  Call -> Reject by Responder.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.91.0/24", "192.168.91.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.91.30"})

  script_a = f"connect, wait 5, message {id_b} \"init\", wait 15, call {id_b}, wait 35, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "/ip4/192.168.91.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.91.31"})

  script_b = f"connect, wait 30, reject {id_a}, wait 35, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "/ip4/192.168.91.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.91.32"})

  scenario.expect_logs("app-a", ["Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=85, stop_on_first_exit=False)

def test_call_accept_hangup_caller(test_environment):
  """
  Call -> Accept -> Talk -> Hangup by Caller.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.92.0/24", "192.168.92.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.92.30"})

  script_a = f"connect, wait 5, message {id_b} \"init\", wait 15, call {id_b}, wait 25, hangup {id_b}, wait 20, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "/ip4/192.168.92.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.92.31"})

  script_b = f"connect, wait 30, accept {id_a}, wait 45, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "/ip4/192.168.92.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.92.32"})

  scenario.expect_logs("app-b", ["[mock] Call accepted", "[mock] Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=95, stop_on_first_exit=False)

def test_call_accept_hangup_responder(test_environment):
  """
  Call -> Accept -> Talk -> Hangup by Responder.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.93.0/24", "192.168.93.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.93.30"})

  script_a = f"connect, wait 5, message {id_b} \"init\", wait 15, call {id_b}, wait 40, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "/ip4/192.168.93.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.93.31"})

  script_b = f"connect, wait 30, accept {id_a}, wait 15, hangup {id_a}, wait 15, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "/ip4/192.168.93.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.93.32"})

  scenario.expect_logs("app-a", ["[mock] Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=95)

def test_call_reject_send_message(test_environment):
  """
  Call -> Reject by Responder -> Responder sends message.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.94.0/24", "192.168.94.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.94.30"})

  script_a = f"connect, wait 5, message {id_b} \"init\", wait 15, call {id_b}, wait 30, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "/ip4/192.168.94.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.94.31"})

  script_b = f"connect, wait 35, reject {id_a}, message {id_a} \"Sorry busy\", wait 25, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "/ip4/192.168.94.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.94.32"})

  scenario.expect_logs("app-a", ["[mock] Call terminated", f"[mock] Message from {id_b} received: Sorry busy"])
  test_environment.execute_scenario(scenario, timeout_seconds=85)
