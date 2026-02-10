import time
import docker
from typing import List, Dict, Optional
from dataclasses import dataclass, field
from . import docker_utils  # Relative import within the package

@dataclass
class NetworkDef:
  name: str
  subnet: str
  gateway: str

@dataclass
class ProcessDef:
  name: str
  executable: str
  args: List[str]
  # Maps network_name -> ip_address
  networks: Dict[str, str] = field(default_factory=dict)

@dataclass
class LogExpectation:
  process_name: str
  expected_lines: List[str]

class Scenario:
  def __init__(self):
    self.networks: List[NetworkDef] = []
    self.processes: List[ProcessDef] = []
    self.expectations: List[LogExpectation] = []

  def add_network(self, name: str, subnet: str, gateway: str):
    self.networks.append(NetworkDef(name, subnet, gateway))
    return self

  def add_process(self, name: str, executable: str, args: List[str], networks: Dict[str, str]):
    self.processes.append(ProcessDef(name, executable, args, networks))
    return self

  def expect_logs(self, process_name: str, lines: List[str]):
    self.expectations.append(LogExpectation(process_name, lines))
    return self

class TestFramework:
  def __init__(self, docker_client: docker.DockerClient, image_tag: str, bin_path: str):
    self.client = docker_client
    self.image_tag = image_tag
    self.bin_path = bin_path
    self.active_containers: List[docker.models.containers.Container] = []
    self.active_networks: List[docker.models.networks.Network] = []

  def gen_identity(self):
    """Helper to generate an identity using the compiled mercury-mock-app."""
    volumes = {
      self.bin_path: {'bind': '/app/bin', 'mode': 'rw'}
    }
    executable = "/app/bin/mercury-mock-app"

    try:
      output = self.client.containers.run(
        self.image_tag,
        command=[executable, "--gen-identity"],
        volumes=volumes,
        remove=True
      ).decode("utf-8")
    except Exception as e:
      print(f"Failed to generate identity: {e}")
      raise

    mnemonic = ""
    pid = ""
    for line in output.splitlines():
      if line.startswith("MNEMONIC:"):
        mnemonic = line.replace("MNEMONIC:", "").strip()
      if line.startswith("ID:"):
        pid = line.replace("ID:", "").strip()
    return pid, mnemonic

  def execute_scenario(self, scenario: Scenario, timeout_seconds: int = 10):
    print(f"\n--- Executing Scenario with {len(scenario.processes)} processes ---")

    # 1. Setup Networks
    net_map = {}
    for net_def in scenario.networks:
      try:
        network = docker_utils.create_network(
          self.client, net_def.name, net_def.subnet, net_def.gateway
        )
        self.active_networks.append(network)
        net_map[net_def.name] = network
      except Exception as e:
        print(f"Failed to create network {net_def.name}: {e}")
        self.teardown()
        raise

    # 2. Start Processes
    container_map = {}

    # Mount the binary directory to /app/bin
    volumes = {
      self.bin_path: {'bind': '/app/bin', 'mode': 'rw'}
    }

    for proc_def in scenario.processes:
      full_command = [proc_def.executable] + proc_def.args
      print(f"Starting {proc_def.name}...")

      try:
        container = docker_utils.run_container(
          self.client,
          self.image_tag,
          proc_def.name,
          command=full_command,
          volumes=volumes,
          detach=True
        )
        self.active_containers.append(container)
        container_map[proc_def.name] = container

        # Connect to requested networks
        for net_name, ip in proc_def.networks.items():
          if net_name not in net_map:
            raise ValueError(f"Process {proc_def.name} requests unknown network {net_name}")
          net_map[net_name].connect(container, ipv4_address=ip)

      except Exception as e:
        print(f"Failed to start process {proc_def.name}: {e}")
        self.teardown()
        raise

    # 3. Wait for execution
    print(f"Waiting {timeout_seconds}s for test execution...")
    time.sleep(timeout_seconds)

    # 4. Verify Logs
    print("\n--- Verifying Logs ---")
    failures = []

    for expectation in scenario.expectations:
      if expectation.process_name not in container_map:
        failures.append(f"Expectation for unknown process: {expectation.process_name}")
        continue

      container = container_map[expectation.process_name]
      logs = container.logs().decode('utf-8')

      # Print logs for debug visibility
      print(f"[{expectation.process_name}] Logs:\n{logs}\n")

      last_idx = -1
      for line in expectation.expected_lines:
        idx = logs.find(line, last_idx + 1)
        if idx == -1:
          failures.append(
            f"Process '{expectation.process_name}': Expected line '{line}' not found "
            f"(or not found after previous match)."
          )
          break # Stop checking this process
        last_idx = idx

    if failures:
      print("FAILED assertions:")
      for f in failures:
        print(f"  - {f}")
      raise AssertionError(f"Scenario failed with {len(failures)} log mismatches.")

    print("Scenario PASSED.")

  def teardown(self):
    print("\n--- Teardown ---")
    for c in self.active_containers:
      try:
        c.stop()
        c.remove()
      except:
        pass
    self.active_containers.clear()

    for n in self.active_networks:
      try:
        n.remove()
      except:
        pass
    self.active_networks.clear()
