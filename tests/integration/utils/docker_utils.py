import docker
import sys
from pathlib import Path
from typing import Iterator, Optional, Generator

def docker_client() -> Generator[docker.DockerClient, None, None]:
  """Yields a docker client instance, with Colima fallback."""
  client = None
  try:
    client = docker.from_env()
    client.ping()
  except Exception:
    # Fallback for Colima on macOS
    colima_socket = Path.home() / ".colima" / "default" / "docker.sock"
    if colima_socket.exists():
      client = docker.DockerClient(base_url=f"unix://{colima_socket}")
      try:
        client.ping()
      except Exception as e:
        print(f"Failed to connect to Colima socket: {e}")
        raise e
    else:
      raise

  try:
    yield client
  finally:
    if client:
      client.close()

def create_network(client: docker.DockerClient, name: str, subnet: str, gateway: str):
  """Creates a docker bridge network, removing any existing one with the same name."""
  try:
    existing = client.networks.get(name)
    print(f"Removing existing network: {name}")
    existing.remove()
  except docker.errors.NotFound:
    pass

  print(f"Creating network {name} ({subnet})")
  ipam_pool = docker.types.IPAMPool(subnet=subnet, gateway=gateway)
  ipam_config = docker.types.IPAMConfig(pool_configs=[ipam_pool])
  return client.networks.create(name=name, driver="bridge", ipam=ipam_config)

def build_image_if_not_exists(client: docker.DockerClient, tag: str, dockerfile_path: str, context_path: str):
  """Builds the docker image if it doesn't exist, streaming logs to stdout."""
  try:
    client.images.get(tag)
    print(f"Image {tag} found.")
  except docker.errors.ImageNotFound:
    print(f"Building image {tag} from {context_path}...")
    # Stream output to avoid silence during build
    build_log = client.api.build(
      path=context_path,
      dockerfile=dockerfile_path,
      tag=tag,
      decode=True,
      rm=True
    )
    for chunk in build_log:
      if 'stream' in chunk:
        sys.stdout.write(chunk['stream'])
        sys.stdout.flush()
      if 'error' in chunk:
        print(f"Error: {chunk['error']}", file=sys.stderr)
        raise Exception("Docker build failed")

def run_compile_container(client: docker.DockerClient, image: str, command: str, volumes: dict):
  """Runs a container for compilation, streaming logs immediately."""
  container = client.containers.run(
    image,
    command=command,
    volumes=volumes,
    detach=True,
    remove=False
  )

  # Stream logs
  for line in container.logs(stream=True):
    sys.stdout.write(line.decode('utf-8'))
    sys.stdout.flush()

  result = container.wait()
  container.remove()

  if result['StatusCode'] != 0:
    raise Exception(f"Compilation container exited with code {result['StatusCode']}")

def run_container(
    client: docker.DockerClient,
    image: str,
    name: str,
    command: Optional[list] = None,
    volumes: Optional[dict] = None,
    environment: Optional[dict] = None,
    detach: bool = True,
):
  """Runs a docker container, ensuring any previous instance with the same name is removed."""
  try:
    existing = client.containers.get(name)
    existing.stop()
    existing.remove()
  except docker.errors.NotFound:
    pass

  container = client.containers.create(
    image=image,
    name=name,
    command=command,
    detach=detach,
    volumes=volumes,
    environment=environment,
    cap_add=["NET_ADMIN", "SYS_NICE"]
  )
  # Don't connect to network here, let the caller handle multiple network connections

  container.start()
  return container
