"""
Security regression scenarios for the 2026-10/11 audit fixes.

Attacker behaviour is driven through test-only commands of mercury-mock-app that send crafted protocol
messages over the real libp2p transport (see the "Attack commands" section in mercury-mock-app/main.cpp).
"""
from utils.framework import Scenario

SIGNALING_ARGS = lambda subnet: ["--signaling-url", f"/ip4/{subnet}.30/tcp/8000"]


def _base(subnet: str) -> Scenario:
  scenario = Scenario()
  scenario.add_network("net", f"{subnet}.0/24", f"{subnet}.1")
  scenario.add_process("signaling", "/app/bin/mercury-bootstrap", ["--port", "8000"], {"net": f"{subnet}.30"})
  return scenario


def _app(scenario: Scenario, subnet: str, name: str, host: int, key: str, db: str, script: str, extra=None):
  args = (extra or []) + ["--key", key] + SIGNALING_ARGS(subnet) + ["--db-path", db, "--script", script]
  scenario.add_process(name, "/app/bin/mercury-mock-app", args, {"net": f"{subnet}.{host}"})


def test_security_unsolicited_confirm_requires_approval(test_environment):
  """Audit C2: a crcp_confirm without proof of our prior approval must not create a confirmed contact."""
  id_victim, key_victim = test_environment.gen_identity()
  id_attacker, key_attacker = test_environment.gen_identity()
  subnet = "192.168.120"
  scenario = _base(subnet)

  _app(scenario, subnet, "victim", 31, key_victim, "/tmp/v.db",
       f"connect, wait 45, status {id_attacker}, wait 5, disconnect", ["--no-auto-confirm"])
  _app(scenario, subnet, "attacker", 32, key_attacker, "/tmp/a.db",
       f"connect, wait 15, dial {id_victim}, wait 10, forge-confirm {id_victim}, wait 20, disconnect")

  scenario.expect_logs("attacker", [f"[mock] Sent forged crcp_confirm to {id_victim}"])
  scenario.expect_logs("victim", [
    "without approval proof",
    f"[mock] Contact request from {id_attacker}",
    f"[mock] STATUS {id_attacker} awaitingApproval",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=70, stop_on_first_exit=False)


def test_security_forged_master_identity_rejected(test_environment):
  """Audit C1: a device cannot claim someone else's (or our own) master identity in key exchange."""
  id_victim, key_victim = test_environment.gen_identity()
  id_attacker, key_attacker = test_environment.gen_identity()
  id_other, _ = test_environment.gen_identity()
  subnet = "192.168.121"
  scenario = _base(subnet)

  _app(scenario, subnet, "victim", 31, key_victim, "/tmp/v.db", "connect, wait 55, disconnect", ["--no-auto-confirm"])
  _app(scenario, subnet, "attacker", 32, key_attacker, "/tmp/a.db",
       f"connect, wait 15, dial {id_victim}, wait 10, "
       f"forge-keyexchange {id_victim} {id_victim}, wait 5, "
       f"forge-keyexchange {id_victim} {id_other}, wait 15, disconnect")

  scenario.expect_logs("attacker", [f"[mock] Sent forged key_exchange to {id_victim} claiming {id_other}"])
  scenario.expect_logs("victim", [
    f"CRCP verification failed for {id_victim}",
    f"CRCP verification failed for {id_other}",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=70, stop_on_first_exit=False)


def test_security_forged_ledger_entry_rejected(test_environment):
  """Audit C4: a confirmed contact cannot inject history entries authored by us."""
  id_victim, key_victim = test_environment.gen_identity()
  id_attacker, key_attacker = test_environment.gen_identity()
  subnet = "192.168.122"
  scenario = _base(subnet)

  _app(scenario, subnet, "victim", 31, key_victim, "/tmp/v.db", "connect, wait 60, disconnect")
  _app(scenario, subnet, "attacker", 32, key_attacker, "/tmp/a.db",
       f"connect, wait 15, message {id_victim} \"hello\", wait 20, forge-ledger {id_victim}, wait 15, disconnect")

  scenario.expect_logs("attacker", [f"[mock] Sent forged ledger entry to {id_victim}"])
  scenario.expect_logs("victim", [
    f"[mock] Message from {id_attacker} received: hello",
    f"[Sync] Dropping unauthenticated ledger entry by {id_victim} relayed by {id_attacker}",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=75, stop_on_first_exit=False)


def test_security_sync_foreign_topic_rejected(test_environment):
  """Audit H1: a contact cannot pull the history of our conversation with somebody else."""
  id_victim, key_victim = test_environment.gen_identity()
  id_friend, key_friend = test_environment.gen_identity()
  id_attacker, key_attacker = test_environment.gen_identity()
  subnet = "192.168.123"
  scenario = _base(subnet)

  _app(scenario, subnet, "victim", 31, key_victim, "/tmp/v.db", "connect, wait 70, disconnect")
  _app(scenario, subnet, "friend", 32, key_friend, "/tmp/f.db",
       f"connect, wait 15, message {id_victim} \"private to victim\", wait 50, disconnect")
  _app(scenario, subnet, "attacker", 33, key_attacker, "/tmp/a.db",
       f"connect, wait 15, message {id_victim} \"hi\", wait 25, sync-steal {id_victim} {id_friend}, wait 20, disconnect")

  scenario.expect_logs("victim", [f"for unauthorized topic {id_friend}"])
  scenario.expect_logs("attacker", [f"[mock] Sent sync request to {id_victim} for topic {id_friend}"])

  test_environment.execute_scenario(scenario, timeout_seconds=85, stop_on_first_exit=False)


def test_security_attachment_name_traversal(test_environment):
  """Audit C3: a peer-supplied attachment name cannot write outside the attachments directory."""
  id_victim, key_victim = test_environment.gen_identity()
  id_attacker, key_attacker = test_environment.gen_identity()
  subnet = "192.168.124"
  scenario = _base(subnet)

  # Victim DB is /tmp/v.db, so its attachments directory is /tmp/attachments.
  _app(scenario, subnet, "victim", 31, key_victim, "/tmp/v.db",
       "connect, wait 60, check-file /tmp/escape.txt, check-file /tmp/attachments/escape.txt, disconnect")
  _app(scenario, subnet, "attacker", 32, key_attacker, "/tmp/a.db",
       f"connect, mkfile /tmp/payload.bin 2048, wait 15, "
       f"attach-named {id_victim} /tmp/payload.bin ../escape.txt, wait 45, disconnect")

  scenario.expect_logs("victim", [
    f"downloaded from {id_attacker}",
    "[mock] FILE MISSING /tmp/escape.txt",
    "[mock] FILE EXISTS /tmp/attachments/escape.txt",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=80, stop_on_first_exit=False)


def test_security_restored_identity_auto_confirms_with_proof(test_environment):
  """P0-2b: after restoring from mnemonic (empty contact list), a contact proving our prior approval is confirmed."""
  id_a, key_a = test_environment.gen_identity()
  id_b, key_b = test_environment.gen_identity()
  subnet = "192.168.125"
  scenario = _base(subnet)

  _app(scenario, subnet, "app-a", 31, key_a, "/tmp/a.db",
       f"connect, wait 15, message {id_b} \"before restore\", wait 35, message {id_b} \"after restore\", wait 45, disconnect")
  # Original device of B befriends A, then goes away.
  _app(scenario, subnet, "app-b-old", 32, key_b, "/tmp/b.db", "connect, wait 35, disconnect")
  # B restored from mnemonic on a fresh device after the old one is gone (the client only starts after the
  # delay, so it cannot sync from the old device): no contacts, no auto-confirm by the mock.
  _app(scenario, subnet, "app-b-new", 33, key_b, "/tmp/b2.db",
       f"connect, wait 50, status {id_a}, disconnect", ["--no-auto-confirm", "--start-delay", "42"])

  scenario.expect_logs("app-b-old", [f"[mock] Message from {id_a} received: before restore"])
  scenario.expect_logs("app-b-new", [
    "with proof of prior approval. Auto-confirming.",
    f"[mock] STATUS {id_a} confirmed",
  ])

  test_environment.execute_scenario(scenario, timeout_seconds=110, stop_on_first_exit=False)
