#!/bin/bash
set -euo pipefail

# --- Configuration ---
readonly PROJECT_ROOT="$(git rev-parse --show-toplevel)"
readonly APP_DIR="${PROJECT_ROOT}/src/apps/mercury-bot"
readonly BUILD_DIR="build-deploy"

# Devenv configuration
readonly DEVENV_IMAGE_NAME="mercury-devenv"
readonly DEVENV_CONTEXT_DIR="${PROJECT_ROOT}/devenv"
readonly IMAGE_NAME_BASE="mercury-bot"

# --- Colors ---
readonly COLOR_BLUE='\033[1;34m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_RED='\033[0;31m'
readonly COLOR_NONE='\033[0m'

# --- Helpers ---
info() { echo -e "${COLOR_BLUE}INFO:${COLOR_NONE} $1"; }
success() { echo -e "${COLOR_GREEN}SUCCESS:${COLOR_NONE} $1"; }
error() { echo -e "${COLOR_RED}ERROR:${COLOR_NONE} $1"; }

run_in_devenv() {
  # Mounts project root to /workspace (matching your manual habits)
  # Mounts SSH keys for git operations
  mkdir -p "${HOME}/.ssh"
  docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "${PROJECT_ROOT}:/workspace" \
    -v "${HOME}/.ssh:/workspace/.ssh" \
    -w /workspace \
    "${DEVENV_IMAGE_NAME}" \
    bash -c '
      # Fix git safety and config inside container
      export GIT_CONFIG_GLOBAL=/workspace/.gitconfig.tmp
      git config --global --add safe.directory /workspace
      exec "$@"
    ' bash "$@"
}

# Define final tags
readonly FINAL_IMAGE_LATEST="${IMAGE_NAME_BASE}:latest"

# --- Argument Parsing ---
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

# --- Main Flow ---

# 1. Prepare Build Environment
# CHANGE: Context is set to 'devenv' dir only.
# This prevents copying the whole repo (gigabytes) into the docker daemon context.
info "Checking/Building development environment image..."
docker build -t "${DEVENV_IMAGE_NAME}" "${DEVENV_CONTEXT_DIR}"

info "Building mercury-bot in Release mode..."

# 2. Build the bot binary
# The source code is now available via the volume mount in run_in_devenv
run_in_devenv bash -c "
  cmake -S . -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build ${BUILD_DIR} -j 20 --target mercury-bot \
  && llvm-strip ${BUILD_DIR}/src/apps/mercury-bot/mercury-bot
"
success "Build complete."

# 3. Stage binary for Docker build
info "Staging binary..."
BIN_SOURCE="${PROJECT_ROOT}/${BUILD_DIR}/src/apps/mercury-bot/mercury-bot"
BIN_DEST="${APP_DIR}/mercury-bot-bin"

if [[ ! -f "${BIN_SOURCE}" ]]; then
  error "Binary not found at ${BIN_SOURCE}"
  exit 1
fi

cp "${BIN_SOURCE}" "${BIN_DEST}"

# 4. Build the final runtime Docker image
# We use APP_DIR as context here, assuming it only contains the Dockerfile and the binary
docker build \
  -t "${FINAL_IMAGE_LATEST}" \
  -f "${APP_DIR}/Dockerfile" \
  "${APP_DIR}"

# 5. Cleanup
rm "${BIN_DEST}"
success "Docker image '${FINAL_IMAGE_LATEST}' created successfully."

# 6. Pack (if requested)
if [[ "${PACK_IMAGE}" == "true" ]]; then
  EXPORT_FILE="${PROJECT_ROOT}/mercury-bot-latest.tar.gz"
  info "Packing image for Synology NAS (saving to ${EXPORT_FILE})..."

  docker save "${FINAL_IMAGE_LATEST}" | gzip > "${EXPORT_FILE}"

  success "Image packed successfully."
fi
