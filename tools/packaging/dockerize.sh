#!/usr/bin/env bash
set -euo pipefail

# --- Configuration Variables ---
readonly PROJECT_ROOT="$(git rev-parse --show-toplevel)"
readonly BUILD_DIR="build-deploy"
readonly DEVENV_IMAGE_NAME="mercury-devenv"
readonly DEVENV_CONTEXT_DIR="${PROJECT_ROOT}/devenv"

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

# --- Argument Parsing ---
TARGET=""
APP_DIR=""
IMAGE_NAME=""
EXPORT_BIN=""
PACK_IMAGE=false
NO_CACHE=false
DEBUG_MODE=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target) TARGET="$2"; shift 2 ;;
    --app-dir) APP_DIR="$2"; shift 2 ;;
    --image-name) IMAGE_NAME="$2"; shift 2 ;;
    --export-bin) EXPORT_BIN="$2"; shift 2 ;;
    --pack) PACK_IMAGE=true; shift ;;
    --no-cache) NO_CACHE=true; shift ;;
    --debug) DEBUG_MODE=true; shift ;;
    *)
      error "Unknown argument: $1"
      echo "Usage: $0 --target <target> --app-dir <dir> [--image-name <name>] [--export-bin <path>] [--pack] [--no-cache] [--debug]"
      exit 1
      ;;
  esac
done

if [[ -z "${TARGET}" || -z "${APP_DIR}" ]]; then
  error "Missing required arguments."
  echo "Usage: $0 --target <target> --app-dir <dir> [--image-name <name>] [--export-bin <path>] [--pack] [--no-cache] [--debug]"
  exit 1
fi

IMAGE_NAME="${IMAGE_NAME:-${TARGET}}"
readonly FULL_APP_DIR="${PROJECT_ROOT}/${APP_DIR}"
readonly FINAL_IMAGE_LATEST="${IMAGE_NAME}:latest"
readonly BIN_SOURCE="${PROJECT_ROOT}/${BUILD_DIR}/${APP_DIR}/${TARGET}"
readonly BIN_DEST="${FULL_APP_DIR}/${TARGET}-bin"

# --- Main Flow ---
info "Checking/Building development environment image..."
if [[ "${NO_CACHE}" == "true" ]]; then
  docker build --no-cache -t "${DEVENV_IMAGE_NAME}" "${DEVENV_CONTEXT_DIR}"
else
  docker build -t "${DEVENV_IMAGE_NAME}" "${DEVENV_CONTEXT_DIR}"
fi

CMAKE_BUILD_TYPE="Release"
STRIP_COMMAND="llvm-strip ${BUILD_DIR}/${APP_DIR}/${TARGET}"

if [[ "${DEBUG_MODE}" == "true" ]]; then
  CMAKE_BUILD_TYPE="Debug"
  STRIP_COMMAND="true"
fi

info "Building ${TARGET} in ${CMAKE_BUILD_TYPE} mode..."
run_in_devenv bash -c "
  cmake -S . -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \\
  && cmake --build ${BUILD_DIR} -j 20 --target ${TARGET} \\
  && ${STRIP_COMMAND}
"
success "Build complete."

info "Staging binary..."
if [[ ! -f "${BIN_SOURCE}" ]]; then
  error "Binary not found at ${BIN_SOURCE}"
  exit 1
fi

cp "${BIN_SOURCE}" "${BIN_DEST}"

if [[ -n "${EXPORT_BIN}" ]]; then
  info "Exporting binary to ${EXPORT_BIN}..."
  cp "${BIN_SOURCE}" "${PROJECT_ROOT}/${EXPORT_BIN}"
fi

info "Building final Docker image '${FINAL_IMAGE_LATEST}'..."
if [[ "${NO_CACHE}" == "true" ]]; then
  docker build --no-cache -t "${FINAL_IMAGE_LATEST}" -f "${FULL_APP_DIR}/Dockerfile" "${FULL_APP_DIR}"
else
  docker build -t "${FINAL_IMAGE_LATEST}" -f "${FULL_APP_DIR}/Dockerfile" "${FULL_APP_DIR}"
fi

info "Cleaning up staged binary..."
rm "${BIN_DEST}"
success "Docker image '${FINAL_IMAGE_LATEST}' created successfully."

if [[ "${PACK_IMAGE}" == "true" ]]; then
  EXPORT_FILE="${PROJECT_ROOT}/${IMAGE_NAME}-latest.tar.gz"
  info "Packing image (saving to ${EXPORT_FILE})..."
  docker save "${FINAL_IMAGE_LATEST}" | gzip > "${EXPORT_FILE}"
  success "Image packed successfully."
fi
