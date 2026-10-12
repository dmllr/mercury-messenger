import pytest
from utils.framework import Scenario

def test_contact_unapproved_message(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.100.0/24", "192.168.100.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.100.30"})

  script_a = f"connect, wait 15, message {id_b} \"Hello\", wait 35, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.100.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.100.31"})

  script_b = "connect, wait 50, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.100.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.100.32"})

  scenario.expect_logs("app-a", [f"Sync error/rejection: Contact {id_b} is not confirmed."])
  scenario.expect_logs("app-b", [f"[mock] Contact request from {id_a}"])

  test_environment.execute_scenario(scenario, timeout_seconds=65, stop_on_first_exit=False)


def test_contact_unapproved_message_reverse(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.101.0/24", "192.168.101.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.101.30"})

  script_a = f"connect, wait 15, call {id_b}, wait 5, hangup {id_b}, wait 35, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_a, "--signaling-url", "/ip4/192.168.101.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.101.31"})

  script_b = f"connect, wait 25, message {id_a} \"Hello\", wait 30, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.101.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.101.32"})

  scenario.expect_logs("app-b", [f"[mock] Message to {id_a} delivered"])
  scenario.expect_logs("app-a", [f"[mock] Message from {id_b} received: Hello"])

  test_environment.execute_scenario(scenario, timeout_seconds=70, stop_on_first_exit=False)


def test_contact_approved_message(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.102.0/24", "192.168.102.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.102.30"})

  script_a = f"connect, wait 15, message {id_b} \"Hello\", wait 40, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.102.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.102.31"})

  script_b = "connect, wait 55, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--key", key_b, "--signaling-url", "/ip4/192.168.102.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.102.32"})

  scenario.expect_logs("app-b", [f"[mock] Message from {id_a} received: Hello"])

  test_environment.execute_scenario(scenario, timeout_seconds=70, stop_on_first_exit=False)


def test_contact_late_approval_sync(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.103.0/24", "192.168.103.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.103.30"})

  script_a = f"connect, wait 15, message {id_b} \"Late Sync Message\", wait 55, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_a, "--signaling-url", "/ip4/192.168.103.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.103.31"})

  script_b = f"connect, wait 25, confirm {id_a}, wait 45, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.103.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.103.32"})

  scenario.expect_logs("app-a", [f"Sync error/rejection: Contact {id_b} is not confirmed."])
  scenario.expect_logs("app-b", [f"[mock] Message from {id_a} received: Late Sync Message"])

  test_environment.execute_scenario(scenario, timeout_seconds=85, stop_on_first_exit=False)


def test_contact_mutual_add(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.104.0/24", "192.168.104.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.104.30"})

  script_a = f"connect, wait 15, message {id_b} \"Hello Mutual\", wait 50, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_a, "--signaling-url", "/ip4/192.168.104.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.104.31"})

  script_b = f"connect, wait 25, call {id_a}, wait 5, hangup {id_a}, wait 35, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.104.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.104.32"})

  scenario.expect_logs("app-b", [f"[mock] Message from {id_a} received: Hello Mutual"])

  test_environment.execute_scenario(scenario, timeout_seconds=80, stop_on_first_exit=False)


def test_call_unapproved_rejected(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.105.0/24", "192.168.105.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.105.30"})

  script_a = f"connect, wait 15, call {id_b}, wait 35, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.105.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.105.31"})

  script_b = "connect, wait 50, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.105.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.105.32"})

  scenario.expect_logs("app-b", ["CRCP Enforcement: Dropped message from unconfirmed or unverified peer"])
  scenario.expect_logs("app-a", ["[mock] Call terminated"])

  test_environment.execute_scenario(scenario, timeout_seconds=65, stop_on_first_exit=False)


def test_call_unapproved_then_approved_sync(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.106.0/24", "192.168.106.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.106.30"})

  script_a = f"connect, wait 15, call {id_b}, wait 10, hangup {id_b}, wait 45, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.106.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.106.31"})

  script_b = f"connect, wait 25, confirm {id_a}, wait 45, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.106.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.106.32"})

  scenario.expect_logs("app-a", ["[mock] Call terminated"])
  scenario.expect_logs("app-b", [f"[mock] Missing call message from {id_a}"])

  test_environment.execute_scenario(scenario, timeout_seconds=85, stop_on_first_exit=False)


def test_call_unapproved_then_approved_then_call(test_environment):
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.107.0/24", "192.168.107.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.107.30"})

  script_a = f"connect, wait 15, call {id_b}, wait 10, hangup {id_b}, wait 25, call {id_b}, wait 45, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.107.30/tcp/8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.107.31"})

  script_b = f"connect, wait 30, confirm {id_a}, wait 30, reject {id_a}, wait 35, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--no-auto-confirm", "--key", key_b, "--signaling-url", "/ip4/192.168.107.30/tcp/8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.107.32"})

  scenario.expect_logs("app-b", [
    "CRCP Enforcement: Dropped message from unconfirmed or unverified peer",
    f"[mock] Missing call message from {id_a}",
    f"[mock] Incoming call from {id_a}",
    f"[mock] Missing call message from {id_a}"
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=65)
