#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

mkdir -p "${ENGINE_DIR}/Saved/Logs"
echo "Starting Epicode headless service. HTTP port from DefaultEpicode.ini, default 8710."
exec "${EDITOR_CMD_BIN}" "${PROJECT_FILE}" /Engine/Maps/Entry -unattended -nosplash -nullrhi -stdout -FullStdOutLogOutput "$@"
