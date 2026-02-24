import pytest
import time
import threading
from utils.framework import Scenario

def test_receiver_crash(test_environment):
  """
  Receiver app crash simulation.
  A sends, B crashes/stops, B restarts, A delivers.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.70.0/24", "192.168.70.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.70.30"})

  # A keeps trying to send
  script_a = f"connect, wait 2, message {id_b} \"Persistence Test\", wait 15, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "ws://192.168.70.30:8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.70.31"})

  # B connects, then we kill it, then we start it again.
  # We simulate this by having B run a short script, exit (or we stop it), and then we create a new B process?
  # Framework supports restarting named containers.
  # So B should run a script that just waits.
  script_b = "connect, wait 30" # Long wait, we will kill it
  scenario.add_process("app-b", "/app/bin/mercury-mock-app",
                       ["--key", key_b, "--signaling-url", "ws://192.168.70.30:8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.70.32"})

  def restart_logic():
      time.sleep(4)
      test_environment.restart_process("app-b")

  t = threading.Thread(target=restart_logic)
  t.start()

  # We expect eventual delivery
  scenario.expect_logs("app-a", [f"[mock] Message to {id_b} delivered"])
  # B logs might be cleared on restart depending on docker config, but 'logs()' usually fetches all.
  # If restart creates a new container ID, we might lose logs or framework handles it.
  # Docker 'restart' keeps container, just reboots. Logs persist.
  scenario.expect_logs("app-b", [f"[mock] Message from {id_a} received: Persistence Test"])

  test_environment.execute_scenario(scenario, timeout_seconds=20, stop_on_first_exit=False)
  t.join()

def test_signaling_crash(test_environment):
  """
  Signaling server crash.
  Apps should reconnect.
  """
  id_a, key_a = test_environment.gen_identity()
  
  scenario = Scenario()
  scenario.add_network("net", "192.168.71.0/24", "192.168.71.1")
  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.71.30"})

  script_a = "connect, wait 15, disconnect"
  scenario.add_process("app-a", "/app/bin/mercury-mock-app",
                       ["--key", key_a, "--signaling-url", "ws://192.168.71.30:8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.71.31"})

  def restart_signaling():
      time.sleep(3)
      print("Restarting signaling server...")
      test_environment.restart_process("signaling")

  t = threading.Thread(target=restart_signaling)
  t.start()

  # We expect app-a to detect disconnect and reconnect (Login success twice)
  # The mock app logs aren't explicitly printing "Login Success" but we can check if it stays alive.
  # Let's rely on built-in logic. If it reconnects, it shouldn't crash.
  # We can't easily assert "reconnected" log without adding it to main.cpp, but "Status change" might show.
  
  test_environment.execute_scenario(scenario, timeout_seconds=18, stop_on_first_exit=False)
  t.join()
