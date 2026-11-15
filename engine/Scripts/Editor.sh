#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

usage() {
  cat <<'EOF'
Usage: Editor.sh [--focused|--legacy] [Unreal arguments...]

  --focused  Start the Epicode focused editor shell (default).
  --legacy   Start the full Unreal Editor with the legacy Epicode tabs.
  --help     Show this help.

Any other argument is passed through to UnrealEditor.
EOF
}

epicode_args=()
requested_focused=0
requested_legacy=0

for arg in "$@"; do
  case "${arg}" in
    --focused)
      requested_focused=1
      ;;
    --legacy)
      requested_legacy=1
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown Epicode option: ${arg}" >&2
      usage >&2
      exit 2
      ;;
    *)
      epicode_args+=("${arg}")
      ;;
  esac
done

if (( requested_focused && requested_legacy )); then
  echo "--focused and --legacy are mutually exclusive" >&2
  exit 2
fi

if (( requested_legacy )); then
  for arg in "${epicode_args[@]}"; do
    if [[ "${arg}" == "-EpicodeFocusedEditor" ]]; then
      echo "-EpicodeFocusedEditor cannot be combined with --legacy" >&2
      exit 2
    fi
  done
else
  has_focused_flag=0
  for arg in "${epicode_args[@]}"; do
    if [[ "${arg}" == "-EpicodeFocusedEditor" ]]; then
      has_focused_flag=1
      break
    fi
  done
  if (( ! has_focused_flag )); then
    epicode_args+=("-EpicodeFocusedEditor")
  fi
fi

mkdir -p "${ENGINE_DIR}/Saved/Logs"
exec "${EDITOR_BIN}" "${PROJECT_FILE}" "${epicode_args[@]}"
