#!/usr/bin/env bash
set -e

PORT="${MERCURY_PORT:-${PORT:-}}"
TURN_PORT="${MERCURY_TURN_PORT:-${TURN_PORT:-}}"
TURN_PORT_RANGE="${MERCURY_TURN_PORT_RANGE:-${TURN_PORT_RANGE:-}}"
IDENTITY_FILE="${MERCURY_IDENTITY_FILE:-${IDENTITY_FILE:-}}"
LOG_LEVEL="${MERCURY_LOG_LEVEL:-${LOG_LEVEL:-}}"

has_flag() {
  local flag="$1"
  shift
  for arg in "$@"; do
    if [[ "$arg" == "$flag" || "$arg" == "$flag="* ]]; then
      return 0
    fi
  done
  return 1
}

# Allow running custom commands directly (e.g. bash, sh)
if [[ $# -gt 0 && ! "$1" =~ ^- ]]; then
  exec "$@"
fi

args=()

if ! has_flag "--port" "$@"; then
  args+=(--port "${PORT:-10807}")
fi

if [[ -n "$TURN_PORT" ]] && ! has_flag "--turn-port" "$@"; then
  args+=(--turn-port "$TURN_PORT")
fi

if [[ -n "$TURN_PORT_RANGE" ]] && ! has_flag "--turn-port-range" "$@"; then
  args+=(--turn-port-range "$TURN_PORT_RANGE")
fi

if ! has_flag "--identity-file" "$@"; then
  args+=(--identity-file "${IDENTITY_FILE:-/data/bootstrap_identity.key}")
fi

if [[ -n "$LOG_LEVEL" ]] && ! has_flag "--log-level" "$@"; then
  args+=(--log-level "$LOG_LEVEL")
fi

exec /app/mercury-bootstrap "${args[@]}" "$@"
