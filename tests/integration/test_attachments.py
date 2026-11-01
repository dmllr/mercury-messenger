import pytest
from utils.framework import Scenario


def test_attachments(test_environment):
  """
  Message with 1, 2, 4 attachments.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.80.0/24", "192.168.80.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.80.30"})

  # A generates files and sends them
  # mkfile <path> <size>
  # attach <id> "msg" <path>...
  script_a = (
    "mkfile /tmp/f1.dat 1024, "
    "mkfile /tmp/f2.dat 2048, "
    "mkfile /tmp/f3.dat 100, "
    "mkfile /tmp/f4.dat 500, "
    "mkfile /tmp/f5.dat 1024, "
    "mkfile /tmp/f6.dat 2048, "
    "mkfile /tmp/f7.dat 100, "
    "connect, wait 5, "
    f"message {id_b} \"init\", "
    "wait 15, "
    f"attach {id_b} \"1 file\" /tmp/f1.dat, "
    "wait 15, "
    f"attach {id_b} \"2 files\" /tmp/f2.dat /tmp/f3.dat, "
    "wait 15, "
    f"attach {id_b} \"4 files\" /tmp/f4.dat /tmp/f5.dat /tmp/f6.dat /tmp/f7.dat, "
    "wait 45, disconnect"
  )

  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.80.30/tcp/8000", "--db-path", "/tmp/a.db",
                        "--script", script_a],
                       {"net": "192.168.80.31"})

  script_b = f"connect, wait 100, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--key", key_b, "--signaling-url", "/ip4/192.168.80.30/tcp/8000", "--db-path", "/tmp/b.db",
                        "--script", script_b],
                       {"net": "192.168.80.32"})

  # Expectations
  scenario.expect_logs("app-a", [
    f"Message with 1 attachments sent to {id_b}",
    "[mock] File transfer status changed to 2",
    f"Message with 2 attachments sent to {id_b}",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
    f"Message with 4 attachments sent to {id_b}",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
  ])

  scenario.expect_logs("app-b", [
    f"[mock] Message from {id_a} received: 1 file",
    "[mock] File transfer status changed to 2",
    f"[mock] Message from {id_a} received: 2 files",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
    f"[mock] Message from {id_a} received: 4 files",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
    "[mock] File transfer status changed to 2",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=120, stop_on_first_exit=False)


def test_large_attachment(test_environment):
  """
  Large attachment with progress.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.81.0/24", "192.168.81.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": "192.168.81.30"})

  # 5MB file
  script_a = (
    "mkfile /tmp/large.dat 5242880, "
    "connect, wait 5, "
    f"message {id_b} \"init\", "
    "wait 15, "
    f"attach {id_b} \"Large\" /tmp/large.dat, "
    "wait 80, disconnect"
  )
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.81.30/tcp/8000", "--db-path", "/tmp/a.db",
                        "--script", script_a], {"net": "192.168.81.31"})

  script_b = f"connect, wait 110, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--key", key_b, "--signaling-url", "/ip4/192.168.81.30/tcp/8000", "--db-path", "/tmp/b.db",
                        "--script", script_b], {"net": "192.168.81.32"})

  scenario.expect_logs("app-b", [
    f"[mock] Message from {id_a} received: Large",
    f"downloaded from {id_a}",
    "[mock] File transfer status changed to 2",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=80, stop_on_first_exit=False)


def test_huge_attachment_relayed(test_environment):
  """
  100MB+ file transfer strictly over a relay to ensure
  the bootstrap node doesn't interrupt or drop large relayed streams.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  # Isolate networks so direct P2P is impossible, forcing relay via Bootstrap Node
  scenario.add_network("net-a", "192.168.82.0/24", "192.168.82.1")
  scenario.add_network("net-b", "192.168.83.0/24", "192.168.83.1")

  # Bootstrap node on both networks
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], 
                       {"net-a": "192.168.82.30", "net-b": "192.168.83.30"})

  # 105MB file (105 * 1024 * 1024 = 110100480 bytes)
  script_a = (
    "mkfile /tmp/huge.dat 110100480, "
    "connect, wait 5, "
    f"message {id_b} \"init\", "
    "wait 15, "
    f"attach {id_b} \"Huge Relayed\" /tmp/huge.dat, "
    "wait 150, disconnect"
  )
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "/ip4/192.168.82.30/tcp/8000", "--db-path", "/tmp/a.db",
                        "--script", script_a], {"net-a": "192.168.82.31"})

  script_b = f"connect, wait 180, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--key", key_b, "--signaling-url", "/ip4/192.168.83.30/tcp/8000", "--db-path", "/tmp/b.db",
                        "--script", script_b], {"net-b": "192.168.83.32"})

  scenario.expect_logs("app-b", [
    f"[mock] Message from {id_a} received: Huge Relayed",
    f"downloaded from {id_a}",
    "[mock] File transfer status changed to 2",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=190, stop_on_first_exit=False)
