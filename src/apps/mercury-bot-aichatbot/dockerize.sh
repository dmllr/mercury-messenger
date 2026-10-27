#!/bin/bash
set -euo pipefail

# --- Configuration ---
readonly PROJECT_ROOT="$(git rev-parse --show-toplevel)"
readonly APP_DIR="${PROJECT_ROOT}/src/apps/mercury-llm-chatbot"
readonly BUILD_DIR="build-deploy"

readonly DEVENV_IMAGE_NAME="mercury-devenv"
readonly DEVENV_CONTEXT_DIR="${PROJECT_ROOT}/devenv"
readonly IMAGE_NAME_BASE="mercury-llm-chatbot"

# --- Colors ---
readonly COLOR_BLUE='\033[1;34m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_RED='\033[0;31m'
readonly COLOR_NONE='\033[0m'

info() { echo -e "${COLOR_BLUE}INFO:${COLOR_NONE} $1"; }
success() { echo -e "${COLOR_GREEN}SUCCESS:${COLOR_NONE} $1"; }
error() { echo -e "${COLOR_RED}ERROR:${COLOR_NONE} $1"; }

run_in_devenv() {
  mkdir -p "${HOME}/.ssh"
  docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "${PROJECT_ROOT}:/workspace" \
    -v "${HOME}/.ssh:/workspace/.ssh" \
    -w /workspace \
    "${DEVENV_IMAGE_NAME}" \
    bash -c '
      export GIT_CONFIG_GLOBAL=/workspace/.gitconfig.tmp
      git config --global --add safe.directory /workspace
      exec "$@"
    ' bash "$@"
}

readonly FINAL_IMAGE_LATEST="${IMAGE_NAME_BASE}:latest"
PACK_IMAGE=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pack)
      PACK_IMAGE=true
      shift
      ;;
    *)
      echo "Unknown argument: $1"
      echo "Usage: $0 [--pack]"
      exit 1
      ;;
  esac
done

info "Building development environment image..."
docker build -t "${DEVENV_IMAGE_NAME}" "${DEVENV_CONTEXT_DIR}"

info "Building mercury-llm-chatbot in Release mode..."
run_in_devenv bash -c "
  cmake -S . -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build ${BUILD_DIR} -j 20 --target mercury-llm-chatbot \
  && llvm-strip ${BUILD_DIR}/src/apps/mercury-llm-chatbot/mercury-llm-chatbot
"
success "Build complete."

info "Staging binary..."
BIN_SOURCE="${PROJECT_ROOT}/${BUILD_DIR}/src/apps/mercury-llm-chatbot/mercury-llm-chatbot"
BIN_DEST="${APP_DIR}/mercury-llm-chatbot-bin"

cp "${BIN_SOURCE}" "${BIN_DEST}"

docker build \
  -t "${FINAL_IMAGE_LATEST}" \
  -f "${APP_DIR}/Dockerfile" \
  "${APP_DIR}"

rm "${BIN_DEST}"
success "Docker image '${FINAL_IMAGE_LATEST}' created successfully."
