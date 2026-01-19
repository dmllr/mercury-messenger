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

  # Logic:
  # 1. git ls-files -z -- src tests     -> Only list tracked files in src and tests
  # 2. grep -z -v -E "..."              -> Exclude specific Android subdirectories
  # 3. grep -z -E "..."                 -> Filter for C/C++ extensions

  run_in_docker bash -c '
    # Define exclusion pattern for format (escaped for grep -E)
    EXCLUDE_PATTERN="src/apps/android/(build|app/(\.cxx|build|release|src/main/cpp/_imported))"

    git ls-files -z -- src tests \
    | grep -z -v -E "$EXCLUDE_PATTERN" \
    | grep -z -E "\.(cpp|hpp|h)$" \
    | while IFS= read -r -d "" file; do
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

  run_in_docker bash -c '
    REGEX_TESTS="^/work/tests/.*"
    REGEX_SRC="^/work/src/(?!apps/android/(build|app/(\.cxx|build|release|src/main/cpp/_imported))).*"
    REGEX_HEADER_FILTER="^/work/(src|tests)/"

    run-clang-tidy-18 \
      -config-file '"/work/.clang-tidy"' \
      -p '"${build_dir}"' \
      -j $(nproc) \
      -header-filter="$REGEX_HEADER_FILTER" \
      "$REGEX_TESTS" "$REGEX_SRC"
  ' || {
    warn "clang-tidy reported issues. Please review the changes or re-run to confirm fixes."
  }

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
