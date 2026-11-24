#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

SERVER_ADDRESS="${EPICODE_SERVER:-127.0.0.1:7777}"
PLAYER_NAME="${EPICODE_PLAYER_NAME:-Player}"
RES_X="${EPICODE_RES_X:-1280}"
RES_Y="${EPICODE_RES_Y:-720}"
MODE="${CLIENT_MODE:-auto}"
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
      RES_X="${2:?--res requires WIDTHxHEIGHT}"
      RES_Y="${RES_X#*x}"
      RES_X="${RES_X%x*}"
      shift 2
      ;;
    --mode)
      MODE="${2:?--mode requires auto|game|editor}"
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

if [[ ! "${MODE}" =~ ^(auto|game|editor)$ ]]; then
  echo "Invalid client mode '${MODE}', expected auto, game or editor." >&2
  exit 2
fi

if [[ ! "${SERVER_ADDRESS}" =~ ^[^:]+:[0-9]+$ ]]; then
  echo "Invalid server address '${SERVER_ADDRESS}', expected host:port." >&2
  exit 2
fi

LOG_DIR="${ENGINE_DIR}/Saved/Logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${EPICODE_CLIENT_LOG:-${LOG_DIR}/client.log}"
FALLBACK_LOG="${LOG_DIR}/client-fallback.log"
CONNECT_CMD="${EPICODE_CONNECT_CMD:-open ${SERVER_ADDRESS}}"

echo "Connecting client '${PLAYER_NAME}' to ${SERVER_ADDRESS}"
echo "Client log: ${LOG_FILE}"

exec_client() {
  local binary="$1"
  shift
  if command -v stdbuf >/dev/null 2>&1; then
    exec stdbuf -oL -eL "${binary}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed \
      -ResX="${RES_X}" -ResY="${RES_Y}" \
      -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
      -ExecCmds="${CONNECT_CMD}" "$@" >>"${LOG_FILE}" 2>&1
  fi
  exec "${binary}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed \
    -ResX="${RES_X}" -ResY="${RES_Y}" \
    -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
    -ExecCmds="${CONNECT_CMD}" "$@" >>"${LOG_FILE}" 2>&1
}

run_client() {
  local binary="$1"
  shift
  CHILD_PID=""
  if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "${binary}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed \
      -ResX="${RES_X}" -ResY="${RES_Y}" \
      -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
      -ExecCmds="${CONNECT_CMD}" "$@" >>"${LOG_FILE}" 2>&1 &
  else
    "${binary}" "${PROJECT_FILE}" /Engine/Maps/Entry -game -windowed \
      -ResX="${RES_X}" -ResY="${RES_Y}" \
      -EpicodeServer="${SERVER_ADDRESS}" -EpicodePlayerName="${PLAYER_NAME}" \
      -ExecCmds="${CONNECT_CMD}" "$@" >>"${LOG_FILE}" 2>&1 &
  fi
  CHILD_PID=$!
  trap 'if [[ -n "${CHILD_PID}" ]]; then kill "${CHILD_PID}" 2>/dev/null || true; fi' TERM INT
  set +e
  wait "${CHILD_PID}"
  status=$?
  set -e
  trap - TERM INT
  CHILD_PID=""
  return "${status}"
}

if [[ "${MODE}" == "game" || ( "${MODE}" == "auto" && -x "${GAME_BIN}" ) ]]; then
  if [[ ! -x "${GAME_BIN}" ]]; then
    echo "Game client binary is missing: ${GAME_BIN}" >&2
    exit 4
  fi
  export EPICODE_CLIENT_MODE="game-binary"
  {
    echo "clientMode=game-binary"
    echo "clientBinary=${GAME_BIN}"
    echo "fallbackReason="
  } > "${FALLBACK_LOG}"
  echo "Client mode: game-binary binary=${GAME_BIN}"
  set +e
  run_client "${GAME_BIN}" "${EXTRA_ARGS[@]}"
  status=$?
  set -e
  if (( status == 0 )); then
    exit 0
  fi
  {
    echo "clientMode=editor-game"
    echo "clientBinary=${EDITOR_BIN}"
    echo "fallbackReason=Game client exited with ${status}."
    echo "fallbackCommand=${EDITOR_BIN} ${PROJECT_FILE} /Engine/Maps/Entry -game"
  } > "${FALLBACK_LOG}"
  if grep -aq "BufferReader" "${LOG_FILE}"; then
    echo "Game client hit BufferReader; falling back to UnrealEditor -game." >&2
  else
    echo "Game client exited with ${status}; falling back to UnrealEditor -game." >&2
  fi
  export EPICODE_CLIENT_MODE="editor-game"
  exec_client "${EDITOR_BIN}" "${EXTRA_ARGS[@]}"
fi

export EPICODE_CLIENT_MODE="editor-game"
{
  echo "clientMode=editor-game"
  echo "clientBinary=${EDITOR_BIN}"
  echo "fallbackReason=Game client not selected."
} > "${FALLBACK_LOG}"
echo "Client mode: editor-game binary=${EDITOR_BIN}"
exec_client "${EDITOR_BIN}" "${EXTRA_ARGS[@]}"
