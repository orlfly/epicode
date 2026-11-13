#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Removing generated build and runtime directories under ${ENGINE_DIR}"
rm -rf "${ENGINE_DIR}/Binaries" "${ENGINE_DIR}/Intermediate" "${ENGINE_DIR}/Saved" "${ENGINE_DIR}/DerivedDataCache"
find "${ENGINE_DIR}/Plugins" -mindepth 2 -maxdepth 2 -type d \( -name Intermediate -o -name Binaries \) -prune -exec rm -rf {} +
echo "Clean complete. Engine installation was not touched."
