#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

SERVER_ADDRESS="${EPICODE_SERVER:-127.0.0.1:7777}"
PLAYER_NAME="${EPICODE_PLAYER_NAME:-Viewer}"
VIEWER_TOKEN="${EPICODE_VIEWER_TOKEN:-}"
CAMERA_MODE="${EPICODE_VIEWER_CAMERA:-free}"
RES_X="${EPICODE_RES_X:-1280}"
RES_Y="${EPICODE_RES_Y:-720}"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --server)
      SERVER_ADDRESS="${2:?--server requires host:port}"
      shift 2
      ;;
    --name)
      PLAYER_NAME="${2:?--name requires a value}"
      shift 2
      ;;
    --token)
      VIEWER_TOKEN="${2:?--token requires a value}"
      shift 2
      ;;
    --camera)
      CAMERA_MODE="${2:?--camera requires a value}"
      shift 2
      ;;
    --res)
      RES_X="${2:?--res requires width}xheight}"
      RES_Y="${RES_X#*x}"
      RES_X="${RES_X%x*}"
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

if [[ ! "${SERVER_ADDRESS}" =~ ^[^:]+:[0-9]+$ ]]; then
  echo "Invalid server address '${SERVER_ADDRESS}', expected host:port." >&2
  exit 2
fi
if [[ ! "${CAMERA_MODE}" =~ ^(free|follow|fixed|preset)$ ]]; then
  echo "Invalid camera mode '${CAMERA_MODE}', expected free, follow, fixed or preset." >&2
  exit 2
fi
if [[ -z "${VIEWER_TOKEN}" ]]; then
  TOKEN_FILE="${EPICODE_VIEWER_TOKEN_FILE:-${ENGINE_DIR}/Saved/Epicode/viewer-token}"
  if [[ -f "${TOKEN_FILE}" ]]; then
    VIEWER_TOKEN="$(tr -d '[:space:]' < "${TOKEN_FILE}")"
  fi
fi
if [[ -z "${VIEWER_TOKEN}" ]]; then
  echo "Missing viewer token. Pass --token or create ${ENGINE_DIR}/Saved/Epicode/viewer-token." >&2
  exit 3
fi

LOG_DIR="${ENGINE_DIR}/Saved/Logs"
mkdir -p "${LOG_DIR}"
export EPICODE_CLIENT_LOG="${EPICODE_VIEWER_LOG:-${LOG_DIR}/viewer.log}"
export EPICODE_CONNECT_CMD="open ${SERVER_ADDRESS}?EpicodeViewer=1?EpicodeViewerToken=${VIEWER_TOKEN}?EpicodeViewerCamera=${CAMERA_MODE}"

exec "${SCRIPT_DIR}/Client.sh" \
  --server "${SERVER_ADDRESS}" \
  --name "${PLAYER_NAME}" \
  --res "${RES_X}x${RES_Y}" \
  -- -EpicodeViewer -EpicodeViewerToken="${VIEWER_TOKEN}" -EpicodeViewerCamera="${CAMERA_MODE}" \
  "${EXTRA_ARGS[@]}"
