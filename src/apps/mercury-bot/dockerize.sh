#!/bin/bash
set -euo pipefail

# --- Configuration ---
readonly PROJECT_ROOT="$(git rev-parse --show-toplevel)"
readonly APP_DIR="${PROJECT_ROOT}/src/apps/mercury-bot"
readonly BUILD_DIR="build-deploy"

# Devenv configuration matches tools/code-health.sh
readonly DEVENV_IMAGE_NAME="project-mercury-devenv"
readonly DEVENV_DOCKERFILE="${PROJECT_ROOT}/devenv/Dockerfile"
readonly FINAL_IMAGE_NAME="mercury-bot"

# --- Colors ---
readonly COLOR_BLUE='\033[1;34m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_NONE='\033[0m'

# --- Helpers ---
info() { echo -e "${COLOR_BLUE}INFO:${COLOR_NONE} $1"; }
success() { echo -e "${COLOR_GREEN}SUCCESS:${COLOR_NONE} $1"; }

run_in_devenv() {
  # Mounts project root to /work and runs command as current user
  # We pass 'bash' as $0 to the inline script so arguments start at $1
  docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "${PROJECT_ROOT}:/work" \
    -w /work \
    "${DEVENV_IMAGE_NAME}" \
    bash -c '
      export GIT_CONFIG_GLOBAL=/work/.gitconfig.tmp
      git config --global --add safe.directory /work
      exec "$@"
    ' bash "$@"
}

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
info "Checking/Building development environment image..."
docker build -t "${DEVENV_IMAGE_NAME}" -f "${DEVENV_DOCKERFILE}" "${PROJECT_ROOT}"

info "Building mercury-bot in Release mode..."

# 2. Build the bot binary
# Note: The build system outputs the binary to 'bin/mercury-bot' inside the build directory
run_in_devenv bash -c "
  cmake -S . -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build ${BUILD_DIR} --target mercury-bot \
  && llvm-strip ${BUILD_DIR}/bin/mercury-bot
"
success "Build complete."

# 3. Stage binary for Docker build
info "Staging binary..."
BIN_SOURCE="${PROJECT_ROOT}/${BUILD_DIR}/bin/mercury-bot"
BIN_DEST="${APP_DIR}/mercury-bot-bin"

if [[ ! -f "${BIN_SOURCE}" ]]; then
  echo "Error: Binary not found at ${BIN_SOURCE}"
  exit 1
fi

cp "${BIN_SOURCE}" "${BIN_DEST}"

# 4. Build the final runtime Docker image
info "Building runtime Docker image '${FINAL_IMAGE_NAME}'..."
docker build -t "${FINAL_IMAGE_NAME}" -f "${APP_DIR}/Dockerfile" "${APP_DIR}"

# 5. Cleanup
rm "${BIN_DEST}"
success "Docker image '${FINAL_IMAGE_NAME}' created successfully."

# 6. Pack (if requested)
if [[ "${PACK_IMAGE}" == "true" ]]; then
  EXPORT_FILE="${PROJECT_ROOT}/mercury-bot.tar.gz"
  info "Packing image for Synology NAS (saving to ${EXPORT_FILE})..."

  # docker save outputs a tar archive, gzip compresses it
  docker save "${FINAL_IMAGE_NAME}" | gzip > "${EXPORT_FILE}"

  success "Image packed successfully."
  info "To deploy on Synology NAS:"
  info "1. Open Container Manager."
  info "2. Go to 'Image' -> 'Import' -> 'Add from File'."
  info "3. Select '${EXPORT_FILE}'."
fi

info "You can run the bot locally:"
info "  docker run -it --rm -v \$(pwd)/identity:/data/identity -v \$(pwd)/output:/data/output ${FINAL_IMAGE_NAME}"
