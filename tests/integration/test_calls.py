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
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.90.30"})

  # A calls B, waits 1s (ringing), then hangs up.
  script_a = f"connect, wait 2, call {id_b}, wait 15, hangup {id_b}, wait 5, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "ws://192.168.90.30:8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.90.31"})

  # B waits for call
  script_b = "connect, wait 20, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "ws://192.168.90.30:8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.90.32"})

  scenario.expect_logs("app-b", [f"[mock] Incoming call from {id_a}", f"[mock] Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=25)

def test_call_reject_by_responder(test_environment):
  """
  Call -> Reject by Responder.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.91.0/24", "192.168.91.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.91.30"})

  script_a = f"connect, wait 2, call {id_b}, wait 20, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "ws://192.168.91.30:8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.91.31"})

  # B receives call, rejects immediately
  script_b = f"connect, wait 10, reject {id_a}, wait 5, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "ws://192.168.91.30:8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.91.32"})

  scenario.expect_logs("app-a", ["Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=25)

def test_call_accept_hangup_caller(test_environment):
  """
  Call -> Accept -> Talk -> Hangup by Caller.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.92.0/24", "192.168.92.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.92.30"})

  script_a = f"connect, wait 2, call {id_b}, wait 15, hangup {id_b}, wait 5, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "ws://192.168.92.30:8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.92.31"})

  script_b = f"connect, wait 10, accept {id_a}, wait 10, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "ws://192.168.92.30:8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.92.32"})

  scenario.expect_logs("app-b", ["Call accepted", "Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=25)

def test_call_accept_hangup_responder(test_environment):
  """
  Call -> Accept -> Talk -> Hangup by Responder.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.93.0/24", "192.168.93.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.93.30"})

  script_a = f"connect, wait 2, call {id_b}, wait 25, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "ws://192.168.93.30:8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.93.31"})

  script_b = f"connect, wait 10, accept {id_a}, wait 5, hangup {id_a}, wait 5, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "ws://192.168.93.30:8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.93.32"})

  scenario.expect_logs("app-a", ["Call terminated"])
  test_environment.execute_scenario(scenario, timeout_seconds=30)

def test_call_reject_send_message(test_environment):
  """
  Call -> Reject by Responder -> Responder sends message.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  scenario = Scenario()
  scenario.add_network("net", "192.168.94.0/24", "192.168.94.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.94.30"})

  script_a = f"connect, wait 2, call {id_b}, wait 25, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", ["--key", key_a, "--signaling-url", "ws://192.168.94.30:8000", "--db-path", "/tmp/a.db", "--script", script_a], {"net": "192.168.94.31"})

  script_b = f"connect, wait 10, reject {id_a}, message {id_a} \"Sorry busy\", wait 5, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", ["--key", key_b, "--signaling-url", "ws://192.168.94.30:8000", "--db-path", "/tmp/b.db", "--script", script_b], {"net": "192.168.94.32"})

  scenario.expect_logs("app-a", ["Call terminated", f"Message from {id_b} received: Sorry busy"])
  test_environment.execute_scenario(scenario, timeout_seconds=30)
