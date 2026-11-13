#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

SERVER_ADDRESS="${EPICODE_SERVER:-127.0.0.1:7777}"
PLAYER_NAME="${EPICODE_DIRECTOR_NAME:-Director}"
PRODUCTION_ID="${EPICODE_PRODUCTION_ID:-}"
SESSION_ID="${EPICODE_SESSION_SCENE:-}"
RES_X="${EPICODE_RES_X:-1600}"
RES_Y="${EPICODE_RES_Y:-900}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
LOG_FILE="${EPICODE_DIRECTOR_LOG:-${LOG_DIR}/director.log}"
EXTRA_ARGS=()

usage() {
  cat <<'EOF'
Usage: Director.sh [options] [-- <unreal args>]

  --server <host:port>     Game server address (default 127.0.0.1:7777)
  --production <id>        Production package identifier
  --session <sceneId>      Session scene identifier
  --name <name>            Director display name
  --res <width>x<height>   Window resolution
  --log <path>             Client log path
  --                       Remaining arguments are passed to Unreal
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --server)
      SERVER_ADDRESS="${2:?--server requires host:port}"
      shift 2
      ;;
    --production)
      PRODUCTION_ID="${2:?--production requires a value}"
      shift 2
      ;;
    --session)
      SESSION_ID="${2:?--session requires a value}"
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
    --log)
      LOG_FILE="${2:?--log requires a path}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! "${SERVER_ADDRESS}" =~ ^[^:]+:[0-9]+$ ]]; then
  echo "Invalid server address '${SERVER_ADDRESS}', expected host:port." >&2
  exit 2
fi
if [[ ! "${RES_X}" =~ ^[0-9]+$ || ! "${RES_Y}" =~ ^[0-9]+$ ]]; then
  echo "Invalid resolution '${RES_X}x${RES_Y}'." >&2
  exit 2
fi

mkdir -p "$(dirname "${LOG_FILE}")"
{
  echo "Epicode director launch"
  echo "role=Director"
  echo "production=${PRODUCTION_ID:-none}"
  echo "session=${SESSION_ID:-none}"
  echo "server=${SERVER_ADDRESS}"
  echo "name=${PLAYER_NAME}"
} >>"${LOG_FILE}"

EXTRA_ARGS+=("-EpicodeDirector")
if [[ -n "${PRODUCTION_ID}" ]]; then
  EXTRA_ARGS+=("-EpicodeProductionId=${PRODUCTION_ID}")
fi
if [[ -n "${SESSION_ID}" ]]; then
  EXTRA_ARGS+=("-EpicodeSessionScene=${SESSION_ID}")
fi

EPICODE_CLIENT_LOG="${LOG_FILE}" "${SCRIPT_DIR}/Client.sh" \
  --server "${SERVER_ADDRESS}" \
  --name "${PLAYER_NAME}" \
  --res "${RES_X}x${RES_Y}" \
  -- "${EXTRA_ARGS[@]}"
