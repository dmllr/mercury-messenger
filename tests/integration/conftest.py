import pytest
import os
from pathlib import Path
from utils import docker_utils

# Constants
IMAGE_TAG = "mercury-test-env:latest"

@pytest.fixture(scope="session")
def project_root():
  """Returns the absolute path to the project root."""
  return str(Path(__file__).parent.parent.parent.absolute())

@pytest.fixture(scope="session")
def docker_client_session():
  """Session-scoped docker client."""
  return next(docker_utils.docker_client())

@pytest.fixture(scope="session")
def build_image(docker_client_session, project_root):
  """
  PHASE 1: Docker Preparation.
  Ensures the test docker image exists.
  """
  print("\n\n=== PHASE 1: DOCKER IMAGE PREPARATION ===")
  dockerfile = "Dockerfile"
  context_path = os.path.join(project_root, "devenv")

  docker_utils.build_image_if_not_exists(
    docker_client_session,
    IMAGE_TAG,
    dockerfile,
    context_path
  )
  print("=== DOCKER IMAGE READY ===\n")
  return IMAGE_TAG

@pytest.fixture(scope="session")
def compile_binaries(docker_client_session, build_image, project_root):
  """
  PHASE 2: Build.
  Compiles the project INSIDE a container to ensure Linux binaries.
  """
  print("=== PHASE 2: COMPILATION ===")

  volumes = {
    project_root: {'bind': '/src', 'mode': 'rw'}
  }

  # We output all binaries to a flat 'bin' directory for easy finding
  cmd = (
    "bash -c '"
    "git config --global --add safe.directory /src && "
    "mkdir -p /src/build-deploy && "
    "cd /src/build-deploy && "
    "if [ -f build.ninja ]; then ninja -t clean; fi && "
    "cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug "
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/src/build-deploy/bin /src && "
    "ninja mercury-mock-app mercury-bootstrap && "
    "ls -l /src/build-deploy/bin"
    "'"
  )

  try:
    docker_utils.run_compile_container(
      docker_client_session,
      IMAGE_TAG,
      cmd,
      volumes
    )
  except Exception as e:
      pytest.exit(f"Compilation inside Docker failed: {e}")

  print("=== COMPILATION COMPLETE ===\n")

  # Return the 'bin' folder path
  return os.path.join(project_root, "build-deploy", "bin")

@pytest.fixture(scope="function")
def test_environment(docker_client_session, compile_binaries):
  """
  PHASE 3: Test Execution Context.
  Provides a framework for running the actual test logic.
  """
  # Import here to avoid issues if utils isn't ready at top level,
  # though __init__.py should fix that.
  from utils.framework import TestFramework

  framework = TestFramework(docker_client_session, IMAGE_TAG, compile_binaries)

  yield framework

  framework.teardown()
