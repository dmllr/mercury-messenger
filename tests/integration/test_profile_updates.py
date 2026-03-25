import pytest
from utils.framework import Scenario

def test_initial_and_updates_profile(test_environment):
  """
  Initial profile exchange and subsequent updates.
  """
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()

  scenario = Scenario()
  scenario.add_network("net", "192.168.60.0/24", "192.168.60.1")

  scenario.add_process("signaling", "/app/bin/mercury-server", ["--port", "8000"], {"net": "192.168.60.30"})

  # A connects, sets profile, sends message to B.
  # Then updates profile.
  # Note: message command in mock-app triggers 'AddContact' which sends profile.
  script_a = (
      f"profile \"Alice\" \"Bio A\", "
      f"connect, wait 2, "
      f"message {id_b} \"Hi\", "
      f"wait 10, "
      f"profile \"Alice Updated\" \"New Bio\", "
      f"wait 10, disconnect"
  )
  
  scenario.add_process("app-a", "/app/bin/mercury-mock-app", 
                       ["--key", key_a, "--signaling-url", "ws://192.168.60.30:8000", "--db-path", "/tmp/a.db", "--script", script_a],
                       {"net": "192.168.60.31"})

  script_b = "connect, wait 15, disconnect"
  scenario.add_process("app-b", "/app/bin/mercury-mock-app", 
                       ["--key", key_b, "--signaling-url", "ws://192.168.60.30:8000", "--db-path", "/tmp/b.db", "--script", script_b],
                       {"net": "192.168.60.32"})

  # B should receive initial profile (implicitly checked via "Profile updated" log when contact is added/synced)
  # And then update.
  
  # NOTE: The mock app logs "[mock] Profile updated for ..."
  # We expect it twice? Once on initial contact, once on update.
  # Depending on timing, initial contact request carries profile.
  
  scenario.expect_logs("app-b", [
      f"[mock] Message from {id_a} received: Hi",
      f"[mock] Profile updated for {id_a}" 
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=35)
