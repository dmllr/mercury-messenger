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
  sysctls: Dict[str, int] = field(default_factory=dict)

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

  def add_process(self, name: str, executable: str, args: List[str], networks: Dict[str, str], sysctls: Dict[str, int] = None):
    self.processes.append(ProcessDef(name, executable, args, networks, sysctls or {}))
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

  def execute_scenario(self, scenario: Scenario, timeout_seconds: int = 10, stop_on_first_exit: bool = True):
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
      print(f"Creating {proc_def.name}...")

      try:
        # Create the STOPPED container
        container = docker_utils.create_container(
          self.client,
          self.image_tag,
          proc_def.name,
          command=full_command,
          volumes=volumes,
          detach=True,
          sysctls=proc_def.sysctls if proc_def.sysctls else None
        )
        self.active_containers.append(container)
        container_map[proc_def.name] = container

        # Enable routing if needed (for router containers)
        if "router" in proc_def.name:
            # We need to set up iptables and ip forwarding
            # This usually requires the container to be running
            pass 
        
        # Attach all networks
        for net_name, ip in proc_def.networks.items():
          if net_name not in net_map:
            raise ValueError(f"Process {proc_def.name} requests unknown network {net_name}")
          net_map[net_name].connect(container, ipv4_address=ip)

        # Disconnect from default bridge to ensure isolation
        try:
          self.client.networks.get("bridge").disconnect(container)
        except Exception:
          pass

        # Start the container
        print(f"Starting {proc_def.name}...")
        container.start()

      except Exception as e:
        print(f"Failed to start process {proc_def.name}: {e}")
        self.teardown()
        raise

    # Configure routes on clients
    # We need to inspect which networks are available and add routes via the router.
    # This logic is scenario-specific, we'll handle it if we can identify topology.
    # For now, let's rely on the tests explicitly setting up routes via 'exec_run' 
    # or assume the test scenarios are simple enough.

    # 3. Wait for execution
    print(f"Waiting up to {timeout_seconds}s for test execution...")
    start_time = time.time()
    while time.time() - start_time < timeout_seconds:
      time.sleep(0.5)
      if stop_on_first_exit:
        any_exited = False
        for name, c in container_map.items():
          try:
            c.reload()
            if c.status == 'exited':
              print(f"Process {name} finished early.")
              any_exited = True
              break
          except docker.errors.NotFound:
            pass
        if any_exited:
          break

    # Gracefully stop remaining processes
    for name, c in container_map.items():
      try:
        c.reload()
        if c.status == 'running':
          print(f"Gracefully stopping {name}...")
          c.stop(timeout=2)
      except docker.errors.NotFound:
        pass

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
          )
          break # Stop checking this process
        last_idx = idx

    if failures:
      print("FAILED assertions:")
      for f in failures:
        print(f"  - {f}")
      raise AssertionError(f"Scenario failed with {len(failures)} log mismatches.")

    print("Scenario PASSED.")

  def get_container(self, name: str):
      for c in self.active_containers:
          if c.name == name:
              return c
      return None

  def stop_process(self, name: str):
      c = self.get_container(name)
      if c:
          print(f"Stopping {name}...")
          c.stop()
  
  def start_process(self, name: str):
      c = self.get_container(name)
      if c:
          print(f"Starting {name}...")
          c.start()

  def restart_process(self, name: str):
    c = self.get_container(name)
    if c:
      print(f"Restarting {name}...")
      c.restart()

  def add_route(self, container_name: str, dest_network: str, via_gateway: str):
    c = self.get_container(container_name)
    if c:
      print(f"Adding route in {container_name}: {dest_network} via {via_gateway}")
      exit_code, output = c.exec_run(f"sh -c 'if ! command -v ip >/dev/null 2>&1; then apt-get update -qq && apt-get install -y iproute2 || apk add --no-cache iproute2; fi; ip route add {dest_network} via {via_gateway}'")
      if exit_code != 0:
        print(f"Failed to add route in {container_name}: {output.decode('utf-8', errors='replace')}")

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
