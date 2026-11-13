#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

SERVER_ADDRESS="${EPICODE_SERVER:-127.0.0.1:7777}"
PLAYER_NAME="${EPICODE_PLAYER_NAME:-Player}"
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

LOG_DIR="${ENGINE_DIR}/Saved/Logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${EPICODE_CLIENT_LOG:-${LOG_DIR}/client.log}"
CONNECT_CMD="${EPICODE_CONNECT_CMD:-open ${SERVER_ADDRESS}}"

echo "Connecting client '${PLAYER_NAME}' to ${SERVER_ADDRESS}"
echo "Client log: ${LOG_FILE}"
if command -v stdbuf >/dev/null 2>&1; then
  exec stdbuf -oL -eL "${CLIENT_BIN}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed -ResX="${RES_X}" -ResY="${RES_Y}" \
    -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
    -ExecCmds="${CONNECT_CMD}" "${EXTRA_ARGS[@]}" >>"${LOG_FILE}" 2>&1
fi
exec "${CLIENT_BIN}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed -ResX="${RES_X}" -ResY="${RES_Y}" \
  -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
  -ExecCmds="${CONNECT_CMD}" "${EXTRA_ARGS[@]}" >>"${LOG_FILE}" 2>&1
