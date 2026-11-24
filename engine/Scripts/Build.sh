#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

CONFIGURATION="${CONFIGURATION:-Development}"
TARGET="${TARGET:-EpicodeVideoEditor}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/build.log"

echo "Building ${TARGET} Linux ${CONFIGURATION} with ${UE_ROOT}"

if [[ "${TARGET}" == "EpicodeVideoServer" ]]; then
  SERVER_LOG="${LOG_DIR}/build-server-target.log"
  set +e
  "${BUILD_SH}" "${TARGET}" Linux "${CONFIGURATION}" -project="${PROJECT_FILE}" -waitmutex 2>&1 \
    | tee "${SERVER_LOG}"
  status=${PIPESTATUS[0]}
  set -e
  if (( status != 0 )); then
    {
      echo "target=${TARGET}"
      echo "configuration=${CONFIGURATION}"
      echo "serverMode=editor-fallback"
      echo "serverTargetSupported=0"
      echo "fallbackReason=EpicodeVideoServer is not supported by this engine distribution; use UnrealEditor-Cmd -server."
      echo "exitCode=${status}"
    } >> "${SERVER_LOG}"
    echo "Server target unavailable; see ${SERVER_LOG}" >&2
    echo "Falling back to UnrealEditor-Cmd -server for runtime sessions." >&2
    exit 6
  fi
  echo "Server target log: ${SERVER_LOG}"
  exit 0
fi

"${BUILD_SH}" "${TARGET}" Linux "${CONFIGURATION}" -project="${PROJECT_FILE}" -waitmutex 2>&1 | tee "${LOG_FILE}"
echo "Build log: ${LOG_FILE}"
