#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

PORT="${EPICODE_GAME_PORT:-7777}"
SCENE_ID="${EPICODE_SESSION_SCENE:-}"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="${2:?--port requires a value}"
      shift 2
      ;;
    --scene)
      SCENE_ID="${2:?--scene requires a value}"
      shift 2
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

if ! [[ "${PORT}" =~ ^[0-9]+$ ]] || (( PORT < 1 || PORT > 65535 )); then
  echo "Invalid game port: ${PORT}" >&2
  exit 2
fi

if command -v ss >/dev/null 2>&1; then
  if ss -H -ltn "sport = :${PORT}" 2>/dev/null | grep -q .; then
    echo "Port ${PORT} is already in use; stop the existing server or choose --port." >&2
    exit 3
  fi
fi

LOG_DIR="${ENGINE_DIR}/Saved/Logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${EPICODE_SERVER_LOG:-${LOG_DIR}/server.log}"
FALLBACK_LOG="${LOG_DIR}/server-fallback.log"

if [[ ( "${SERVER_MODE}" == "auto" || "${SERVER_MODE}" == "server-target" ) && -x "${SERVER_BIN}" ]]; then
  LAUNCH_BIN="${SERVER_BIN}"
  LAUNCH_ARGS=("${PROJECT_FILE}" -server -unattended -nosplash -nullrhi -Port="${PORT}")
  ACTUAL_SERVER_MODE="server-target"
  SERVER_TARGET_SUPPORTED=1
  FALLBACK_REASON=""
elif [[ "${SERVER_MODE}" != "server-target" && -x "${EDITOR_CMD_BIN}" ]]; then
  LAUNCH_BIN="${EDITOR_CMD_BIN}"
  LAUNCH_ARGS=("${PROJECT_FILE}" /Engine/Maps/Entry -server -unattended -nosplash -nullrhi -Port="${PORT}")
  ACTUAL_SERVER_MODE="editor-fallback"
  SERVER_TARGET_SUPPORTED=0
  FALLBACK_REASON="EpicodeVideoServer is unavailable or unsupported; using UnrealEditor-Cmd -server."
else
  echo "No server binary found. Build EpicodeVideoServer or install an engine with server support." >&2
  exit 4
fi

{
  echo "serverMode=${ACTUAL_SERVER_MODE}"
  echo "serverTargetSupported=${SERVER_TARGET_SUPPORTED}"
  echo "serverBinary=${LAUNCH_BIN}"
  echo "fallbackReason=${FALLBACK_REASON}"
} > "${FALLBACK_LOG}"
export EPICODE_SERVER_MODE="${ACTUAL_SERVER_MODE}"
export EPICODE_SERVER_TARGET_SUPPORTED="${SERVER_TARGET_SUPPORTED}"
export EPICODE_SERVER_FALLBACK_REASON="${FALLBACK_REASON}"
export EPICODE_SERVER_BINARY="${LAUNCH_BIN}"

echo "Starting dedicated server on port ${PORT} (scene=${SCENE_ID:-none})"
echo "Server mode: ${ACTUAL_SERVER_MODE} binary=${LAUNCH_BIN}"
echo "Server log: ${LOG_FILE}"
if command -v stdbuf >/dev/null 2>&1; then
  exec stdbuf -oL -eL "${LAUNCH_BIN}" "${LAUNCH_ARGS[@]}" -EpicodeSessionScene="${SCENE_ID}" "${EXTRA_ARGS[@]}" >>"${LOG_FILE}" 2>&1
fi
exec "${LAUNCH_BIN}" "${LAUNCH_ARGS[@]}" -EpicodeSessionScene="${SCENE_ID}" "${EXTRA_ARGS[@]}" >>"${LOG_FILE}" 2>&1
