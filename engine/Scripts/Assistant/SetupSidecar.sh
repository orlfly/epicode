#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VENV_DIR="${EPICODE_ASSISTANT_VENV:-${PROJECT_DIR}/Saved/Epicode/Assistant/venv}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

usage() {
  cat <<'EOF'
Usage: SetupSidecar.sh [--force] [--python PATH]

Creates Saved/Epicode/Assistant/venv and installs
Scripts/Assistant/requirements.txt. The venv is ignored by git.

Set PIP_INDEX_URL to use a mirror, for example:
  PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple SetupSidecar.sh
EOF
}

force=0
while (($# > 0)); do
  case "$1" in
    --force)
      force=1
      shift
      ;;
    --python)
      PYTHON_BIN="${2:?--python requires a path}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "Python interpreter not found: ${PYTHON_BIN}" >&2
  exit 1
fi

if ((force)); then
  rm -rf "${VENV_DIR}"
fi

mkdir -p "$(dirname "${VENV_DIR}")"
if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi

python_in_venv="${VENV_DIR}/bin/python"
pip_in_venv="${VENV_DIR}/bin/pip"

"${python_in_venv}" -m pip install --upgrade pip

pip_args=()
if [[ -n "${PIP_INDEX_URL:-}" ]]; then
  pip_args+=(--index-url "${PIP_INDEX_URL}")
fi

"${pip_in_venv}" install "${pip_args[@]}" -r "${SCRIPT_DIR}/requirements.txt"

"${python_in_venv}" - <<'PY'
import deepseek_harness
import deepseek_harness_runtime
import pydantic

print("deepseek-harness-sdk=" + getattr(deepseek_harness, "__version__", "0.2.2rc2"))
print("pydantic=" + pydantic.__version__)
print("runtime=" + deepseek_harness_runtime.__file__)
PY

echo
echo "Sidecar venv ready: ${VENV_DIR}"
echo "The editor uses it automatically when AssistantBackend=deepseek_sidecar."
