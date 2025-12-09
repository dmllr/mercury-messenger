#!/bin/bash
set -euo pipefail

# --- Configuration ---
readonly DOCKER_IMAGE_NAME="project-mercury-devenv"
readonly PROJECT_ROOT="$(git rev-parse --show-toplevel)"
readonly DOCKERFILE_PATH="${PROJECT_ROOT}/devenv/Dockerfile"

# --- Colors for output ---
readonly COLOR_BLUE='\033[1;34m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_YELLOW='\033[0;33m'
readonly COLOR_NONE='\033[0m'

# --- Helper Functions ---
info() {
  echo -e "${COLOR_BLUE}INFO:${COLOR_NONE} $1"
}

success() {
  echo -e "${COLOR_GREEN}SUCCESS:${COLOR_NONE} $1"
}

warn() {
  echo -e "${COLOR_YELLOW}WARN:${COLOR_NONE} $1"
}

build_docker_image_if_needed() {
  if [[ "$(docker images -q "${DOCKER_IMAGE_NAME}" 2> /dev/null)" == "" ]]; then
    info "Docker image '${DOCKER_IMAGE_NAME}' not found. Building..."
    docker build -t "${DOCKER_IMAGE_NAME}" -f "${DOCKERFILE_PATH}" "${PROJECT_ROOT}/devenv"
    success "Docker image built."
  else
    info "Docker image '${DOCKER_IMAGE_NAME}' found."
  fi
}

run_in_docker() {
  info "Running in Docker: $*"
  # 1. Set a writable global git config inside the project dir to avoid permission errors with //.gitconfig.
  # 2. Add the workspace as a safe directory to fix the 'dubious ownership' error.
  # 3. 'exec "$@"' ensures that the original command is then executed.
  docker run --rm -it \
    --user "$(id -u):$(id -g)" \
    -v "${PROJECT_ROOT}:/work" \
    -w /work \
    "${DOCKER_IMAGE_NAME}" \
    bash -c '
      export GIT_CONFIG_GLOBAL=/work/.gitconfig.tmp
      git config --global --add safe.directory /work
      exec "$@"
    ' bash "$@"
}

run_format() {
  info "Running clang-format..."
  # Use 'git ls-files' to respect .gitignore and only format tracked files.
  # grep -z filters the null-terminated list by extension.
  run_in_docker bash -c '
    git ls-files -z -- src tests | grep -z -E "\.(cpp|hpp|h)$" | while IFS= read -r -d "" file; do
      echo "Formatting $file..."
      clang-format-18 -i "$file"
    done'
  success "clang-format finished."
}

run_tidy() {
  info "Running clang-tidy..."
  local build_dir="build-tidy"

  info "Step 1: Generating compile_commands.json..."
  run_in_docker bash -c "cmake -S . -B ${build_dir} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null"
  success "compile_commands.json generated in '${build_dir}'."

  info "Step 2: Running clang-tidy on the codebase..."
  # run-clang-tidy exits with a non-zero code if it finds issues, even if fixed.
  # We catch this to provide a helpful message instead of letting `set -e` terminate the script.
  if ! run_in_docker bash -c "run-clang-tidy-18 -p ${build_dir} -j $(nproc) -fix"; then
    warn "clang-tidy reported issues. Please review the changes or re-run to confirm fixes."
  fi
  success "clang-tidy finished."
}

# --- Main Script ---
main() {
  if [[ $# -eq 0 ]]; then
    echo "Usage: $0 {format|tidy|all}"
    exit 1
  fi

  build_docker_image_if_needed

  for cmd in "$@"; do
    case "${cmd}" in
      format)
        run_format
        ;;
      tidy)
        run_tidy
        ;;
      all)
        run_format
        run_tidy
        ;;
      *)
        warn "Unknown command: ${cmd}. Skipping."
        echo "Usage: $0 {format|tidy|all}"
        ;;
    esac
  done
}

main "$@"
