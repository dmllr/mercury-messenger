# Code Health Tooling

This directory contains scripts to help maintain code quality using `clang-format` and `clang-tidy`.

The tools are executed inside a Docker container to ensure a consistent environment across different developer machines.

## Prerequisites

-   [Docker](https://www.docker.com/get-started) must be installed and running.
-   On Linux/macOS, you need a standard shell (like `bash`).
-   On Windows, you need Command Prompt or PowerShell.
-   `git` must be installed and available in your `PATH` to locate the project root.

## Setup

The first time you run any command, the script will automatically build a Docker image named `project-mercury-devenv` using `devenv/Dockerfile`. This may take several minutes. Subsequent runs will be much faster as they will reuse the existing image.

## Usage

All scripts should be run from the root of the project repository.

### On Linux or macOS

Use the `code-health.sh` script. Make sure it is executable (`chmod +x ./tools/code-health.sh`).

```sh
# Format all C++ source and header files in-place
./tools/code-health.sh format

# Run clang-tidy and apply automatic fixes
./tools/code-health.sh tidy

# Run both format and tidy
./tools/code-health.sh all
