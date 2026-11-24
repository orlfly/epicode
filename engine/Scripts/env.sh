#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
UE_ROOT="${UE_ROOT:-/usr/local/unreal}"
PROJECT_FILE="${ENGINE_DIR}/EpicodeVideo.uproject"
EDITOR_BIN="${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor"
EDITOR_CMD_BIN="${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd"
BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Linux/Build.sh"
SERVER_BIN="${EPICODE_SERVER_BIN:-${ENGINE_DIR}/Binaries/Linux/EpicodeVideoServer}"
CLIENT_BIN="${EPICODE_CLIENT_BIN:-${EDITOR_BIN}}"
GAME_BIN="${EPICODE_GAME_BIN:-${ENGINE_DIR}/Binaries/Linux/EpicodeVideo}"
SERVER_MODE="${EPICODE_SERVER_MODE:-${SERVER_MODE:-auto}}"
CLIENT_MODE="${EPICODE_CLIENT_MODE:-${CLIENT_MODE:-auto}}"

check_ue() {
  local missing=0
  for path in "${BUILD_SH}" "${EDITOR_BIN}" "${EDITOR_CMD_BIN}"; do
    if [[ ! -x "${path}" ]]; then
      echo "Missing or non-executable Unreal Engine file: ${path}" >&2
      missing=1
    fi
  done
  if [[ ! -f "${PROJECT_FILE}" ]]; then
    echo "Missing project file: ${PROJECT_FILE}" >&2
    missing=1
  fi
  return "${missing}"
}

if [[ "${1:-}" == "--check" ]]; then
  check_ue
  echo "UE_ROOT=${UE_ROOT}"
  echo "PROJECT_FILE=${PROJECT_FILE}"
  echo "EDITOR_BIN=${EDITOR_BIN}"
  echo "GAME_BIN=${GAME_BIN}"
  echo "SERVER_BIN=${SERVER_BIN}"
  echo "CLIENT_BIN=${CLIENT_BIN}"
  echo "SERVER_MODE=${SERVER_MODE}"
  echo "CLIENT_MODE=${CLIENT_MODE}"
  exit 0
fi

if ! check_ue; then
  echo "Set UE_ROOT to a valid Unreal Engine 5.8 installation." >&2
  exit 1
fi

export UE_ROOT ENGINE_DIR PROJECT_FILE EDITOR_BIN EDITOR_CMD_BIN BUILD_SH SERVER_BIN CLIENT_BIN GAME_BIN SERVER_MODE CLIENT_MODE
