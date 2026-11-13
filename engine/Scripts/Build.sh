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
"${BUILD_SH}" "${TARGET}" Linux "${CONFIGURATION}" -project="${PROJECT_FILE}" -waitmutex 2>&1 | tee "${LOG_FILE}"
echo "Build log: ${LOG_FILE}"
