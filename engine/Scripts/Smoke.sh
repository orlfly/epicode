#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

HTTP_PORT="${EPICODE_API_HTTP_PORT:-8710}"
BASE="http://127.0.0.1:${HTTP_PORT}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
TOKEN_FILE="${ENGINE_DIR}/Saved/Epicode/api-token"
EDITOR_LOG="${LOG_DIR}/smoke-editor.log"
mkdir -p "${LOG_DIR}"

EDITOR_PID=""
cleanup() {
  if [[ -n "${EDITOR_PID}" ]] && kill -0 "${EDITOR_PID}" 2>/dev/null; then
    kill "${EDITOR_PID}" 2>/dev/null || true
    for _ in $(seq 1 20); do
      kill -0 "${EDITOR_PID}" 2>/dev/null || break
      sleep 0.25
    done
    kill -9 "${EDITOR_PID}" 2>/dev/null || true
    wait "${EDITOR_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

json_get() {
  python3 -c 'import json,sys
node=json.load(sys.stdin)
for part in sys.argv[1].split("."):
    if part:
        node=node[part]
print(node)' "$1"
}

api() {
  local method="$1"
  local params="${2:-{\}}"
  curl -fsS -X POST "${BASE}/api/v1/command" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -d "{\"method\":\"${method}\",\"params\":${params}}"
}

require_ok() {
  local response="$1"
  local ok
  ok="$(printf '%s' "${response}" | json_get ok)"
  if [[ "${ok}" != "True" ]]; then
    echo "Command failed: ${response}" >&2
    exit 1
  fi
}

echo "Starting headless editor for smoke test"
"${SCRIPT_DIR}/Headless.sh" >"${EDITOR_LOG}" 2>&1 &
EDITOR_PID=$!

echo "Waiting for ${BASE}/api/v1/health"
for _ in $(seq 1 120); do
  if curl -fsS "${BASE}/api/v1/health" >/dev/null 2>&1 && [[ -f "${TOKEN_FILE}" ]]; then
    break
  fi
  if ! kill -0 "${EDITOR_PID}" 2>/dev/null; then
    echo "Editor exited early. Log: ${EDITOR_LOG}" >&2
    tail -60 "${EDITOR_LOG}" >&2 || true
    exit 1
  fi
  sleep 1
done

TOKEN="$(cat "${TOKEN_FILE}")"
UNAUTH_STATUS="$(curl -sS -o /dev/null -w '%{http_code}' -X POST "${BASE}/api/v1/command" \
  -H "Content-Type: application/json" -d '{"method":"system.health","params":{}}')"
if [[ "${UNAUTH_STATUS}" != "401" ]]; then
  echo "expected 401 without token, got ${UNAUTH_STATUS}" >&2
  exit 1
fi
BAD_TYPE_STATUS="$(curl -sS -o /dev/null -w '%{http_code}' -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: text/plain" -d '{}')"
if [[ "${BAD_TYPE_STATUS}" != "415" ]]; then
  echo "expected 415 for text/plain, got ${BAD_TYPE_STATUS}" >&2
  exit 1
fi
INVALID_JSON_STATUS="$(curl -sS -o /dev/null -w '%{http_code}' -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" -d '{invalid')"
if [[ "${INVALID_JSON_STATUS}" != "400" ]]; then
  echo "expected 400 for invalid JSON, got ${INVALID_JSON_STATUS}" >&2
  exit 1
fi
LARGE_BODY="/tmp/epicode-large-body.json"
python3 -c 'import sys; sys.stdout.write("{\"method\":\"system.health\",\"params\":{\"pad\":\"" + "x" * 9000000 + "\"}}")' > "${LARGE_BODY}"
LARGE_STATUS="$(curl -sS -o /dev/null -w '%{http_code}' -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" --data-binary @"${LARGE_BODY}")"
rm -f "${LARGE_BODY}"
if [[ "${LARGE_STATUS}" != "413" ]]; then
  echo "expected 413 for oversized body, got ${LARGE_STATUS}" >&2
  exit 1
fi
echo "auth, content-type, invalid JSON and body-size checks passed"
OUTSIDE_FILE="/tmp/epicode-imports-evil.fbx"
printf 'not a real fbx' > "${OUTSIDE_FILE}"
OUTSIDE_RESULT="$(curl -sS -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
  -d "{\"method\":\"import.run\",\"params\":{\"filePath\":\"${OUTSIDE_FILE}\",\"kind\":\"prop\"}}")"
if [[ "$(printf '%s' "${OUTSIDE_RESULT}" | json_get ok)" == "True" ]]; then
  echo "import outside configured roots was accepted" >&2
  rm -f "${OUTSIDE_FILE}"
  exit 1
fi
rm -f "${OUTSIDE_FILE}"
TOKEN_PERM="$(stat -c '%a' "${TOKEN_FILE}")"
if [[ "${TOKEN_PERM}" != "600" ]]; then
  echo "api-token permissions are ${TOKEN_PERM}, expected 600" >&2
  exit 1
fi
if command -v ss >/dev/null 2>&1; then
  if ! ss -ltn | grep -q "127.0.0.1:${HTTP_PORT}"; then
    echo "HTTP listener is not bound to loopback port ${HTTP_PORT}" >&2
    ss -ltn >&2
    exit 1
  fi
fi
OPTIONS_HEADERS="$(curl -sS -D - -o /dev/null -X OPTIONS "${BASE}/api/v1/rpc" -H "Origin: http://example.invalid" -H "Access-Control-Request-Method: POST")"
if printf '%s' "${OPTIONS_HEADERS}" | grep -qi '^Access-Control-Allow-Origin:'; then
  echo "CORS headers are present while CORS is disabled" >&2
  exit 1
fi
if grep -Fq "${TOKEN}" "${EDITOR_LOG}" 2>/dev/null; then
  echo "API token was written to the editor log" >&2
  exit 1
fi
HEALTH="$(curl -fsS "${BASE}/api/v1/health")"
printf 'health: %s\n' "${HEALTH}"

SCENE_NAME="Smoke A $(date +%H%M%S)"
NEW_SCENE="$(api scene.new "{\"name\":\"${SCENE_NAME}\"}")"
require_ok "${NEW_SCENE}"
SCENE_ID="$(printf '%s' "${NEW_SCENE}" | json_get result.sceneId)"
echo "scene A: ${SCENE_ID}"

GLTF="${ENGINE_DIR}/Tests/Fixtures/cube.gltf"
OBJ="${ENGINE_DIR}/Tests/Fixtures/quad.obj"
FBX="${ENGINE_DIR}/Tests/Fixtures/cube.fbx"
if [[ "${EPICODE_SMOKE_IMPORT:-1}" == "1" ]]; then
  GLTF_RESULT="$(api import.run "{\"filePath\":\"${GLTF}\",\"kind\":\"prop\",\"entityName\":\"SmokeCube\"}")"
  require_ok "${GLTF_RESULT}"
  [[ "$(printf '%s' "${GLTF_RESULT}" | json_get result.assets | wc -l)" -ge 1 ]]
  REUSED_RESULT="$(api import.run "{\"filePath\":\"${GLTF}\",\"kind\":\"prop\",\"entityName\":\"SmokeCube\"}")"
  require_ok "${REUSED_RESULT}"
  printf '%s' "${REUSED_RESULT}" | python3 -c 'import json,sys
result=json.load(sys.stdin)["result"]
if not any("Reused" in warning for warning in result.get("warnings", [])):
    raise SystemExit("second identical import did not reuse indexed assets")'
  echo "import index reused: glTF"
  OBJ_RESULT="$(api import.file "{\"filePath\":\"${OBJ}\",\"kind\":\"prop\",\"entityName\":\"SmokeQuad\"}")"
  require_ok "${OBJ_RESULT}"
  [[ "$(printf '%s' "${OBJ_RESULT}" | json_get result.assets | wc -l)" -ge 1 ]]
  FBX_RESULT="$(api import.run "{\"filePath\":\"${FBX}\",\"kind\":\"prop\",\"entityName\":\"SmokeFbx\"}")"
  require_ok "${FBX_RESULT}"
  [[ "$(printf '%s' "${FBX_RESULT}" | json_get result.assets | wc -l)" -ge 1 ]]
  echo "imported: glTF, OBJ and FBX"
fi

ENTITY="$(api entity.create '{"type":"group","name":"Smoke Root"}')"
require_ok "${ENTITY}"
echo "entity: $(printf '%s' "${ENTITY}" | json_get result.entity.id)"

LIGHT="$(api light.create '{"lightType":"directional","name":"Smoke Sun","intensity":2.0}')"
require_ok "${LIGHT}"
CAMERA="$(api camera.create '{"name":"Smoke Camera","fov":50.0}')"
require_ok "${CAMERA}"

preset="$(api environment.applyPreset '{"preset":"sunset"}')"
require_ok "${preset}"
saved="$(api scene.save '{}')"
require_ok "${saved}"
echo "saved: $(printf '%s' "${saved}" | json_get result.path)"

SCENE_B="$(api scene.new "{\"name\":\"Smoke B $(date +%H%M%S)\"}")"
require_ok "${SCENE_B}"
SCENE_B_ID="$(printf '%s' "${SCENE_B}" | json_get result.sceneId)"
api scene.save '{}' >/dev/null
SWITCHED="$(api scene.switch "{\"sceneId\":\"${SCENE_ID}\"}")"
require_ok "${SWITCHED}"
echo "switched back to: $(printf '%s' "${SWITCHED}" | json_get result.sceneId)"

DELETED="$(api scene.delete "{\"sceneId\":\"${SCENE_B_ID}\"}")"
require_ok "${DELETED}"
echo "deleted scene B: ${SCENE_B_ID}"

SCENES="$(api scene.list '{}')"
require_ok "${SCENES}"
echo "scene count: $(printf '%s' "${SCENES}" | json_get result.scenes | wc -l)"

METHODS="$(api system.methods '{}')"
require_ok "${METHODS}"
printf '%s' "${METHODS}" | python3 -c 'import json,sys
names={m["name"] for m in json.load(sys.stdin)["result"]["methods"]}
missing={"import.file","import.capabilities","import.cancel"}-names
if missing:
    raise SystemExit("missing import commands: "+",".join(sorted(missing)))'
echo "import commands registered: import.file, import.capabilities, import.cancel"

RPC="$(curl -fsS -X POST "${BASE}/api/v1/rpc" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"system.health","params":{}}')"
echo "rpc: $(printf '%s' "${RPC}" | json_get result.status)"

IDEMPOTENCY_KEY="smoke-$(date +%s%N)"
FIRST_IDEMPOTENT="$(curl -sS -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
  -H "Idempotency-Key: ${IDEMPOTENCY_KEY}" \
  -d '{"method":"scene.list","params":{}}')"
require_ok "${FIRST_IDEMPOTENT}"
CONFLICT_STATUS="$(curl -sS -o /tmp/epicode-idempotency-conflict.json -w '%{http_code}' -X POST "${BASE}/api/v1/command" \
  -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
  -H "Idempotency-Key: ${IDEMPOTENCY_KEY}" \
  -d '{"method":"scene.list","params":{"different":true}}')"
if [[ "${CONFLICT_STATUS}" != "409" ]]; then
  echo "expected 409 for reused idempotency key with different params, got ${CONFLICT_STATUS}" >&2
  cat /tmp/epicode-idempotency-conflict.json >&2 || true
  exit 1
fi
echo "idempotency conflict rejected: 409"

if [[ "${EPICODE_SMOKE_RENDER:-0}" == "1" ]]; then
  RENDER_FORMAT="${EPICODE_SMOKE_FORMAT:-png}"
  RENDER_END="${EPICODE_SMOKE_END_FRAME:-3}"
  echo "Submitting ${RENDER_END}-frame ${RENDER_FORMAT} render"
  RENDER="$(api scene.render "{\"format\":\"${RENDER_FORMAT}\",\"width\":320,\"height\":180,\"startFrame\":0,\"endFrame\":${RENDER_END},\"includeAudio\":false}")"
  require_ok "${RENDER}"
  JOB_ID="$(printf '%s' "${RENDER}" | json_get result.jobId)"
  echo "render job: ${JOB_ID}"
  for _ in $(seq 1 180); do
    STATUS="$(api render.status "{\"jobId\":\"${JOB_ID}\"}")"
    STATE="$(printf '%s' "${STATUS}" | json_get result.state)"
    if [[ "${STATE}" == "succeeded" ]]; then
      FILES="$(printf '%s' "${STATUS}" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["result"]["result"]["outputFiles"]))')"
      echo "render succeeded, files: ${FILES}"
      REQUIRED_FILES=1
      if [[ "${RENDER_FORMAT}" != "mp4" ]]; then
        REQUIRED_FILES="${RENDER_END}"
      fi
      if [[ "${FILES}" -lt "${REQUIRED_FILES}" ]]; then
        echo "expected at least ${REQUIRED_FILES} rendered files, got ${FILES}" >&2
        exit 1
      fi
      JOB_DIR="$(printf '%s' "${STATUS}" | json_get result.jobDirectory)"
      if [[ ! -f "${JOB_DIR}/progress.json" ]]; then
        echo "render progress.json missing in ${JOB_DIR}" >&2
        exit 1
      fi
      PROGRESS_STATE="$(json_get state < "${JOB_DIR}/progress.json")"
      if [[ "${PROGRESS_STATE}" != "succeeded" ]]; then
        echo "render progress.json state is ${PROGRESS_STATE}, expected succeeded" >&2
        exit 1
      fi
      echo "render progress.json: ${JOB_DIR}/progress.json"
      break
    fi
    if [[ "${STATE}" == "failed" || "${STATE}" == "cancelled" ]]; then
      echo "render ${STATE}: ${STATUS}" >&2
      exit 1
    fi
    sleep 2
  done
  if [[ "${STATE:-}" != "succeeded" ]]; then
    echo "render timed out: ${STATUS:-}" >&2
    exit 1
  fi
  if [[ "${EPICODE_SMOKE_RETRY:-0}" == "1" ]]; then
    RETRY="$(api render.retry "{\"jobId\":\"${JOB_ID}\"}")"
    require_ok "${RETRY}"
    RETRY_ID="$(printf '%s' "${RETRY}" | json_get result.jobId)"
    echo "retry job: ${RETRY_ID}"
    for _ in $(seq 1 180); do
      RETRY_STATUS="$(api render.status "{\"jobId\":\"${RETRY_ID}\"}")"
      RETRY_STATE="$(printf '%s' "${RETRY_STATUS}" | json_get result.state)"
      if [[ "${RETRY_STATE}" == "succeeded" ]]; then
        echo "render retry succeeded"
        break
      fi
      if [[ "${RETRY_STATE}" == "failed" || "${RETRY_STATE}" == "cancelled" ]]; then
        echo "render retry ${RETRY_STATE}: ${RETRY_STATUS}" >&2
        exit 1
      fi
      sleep 2
    done
    if [[ "${RETRY_STATE:-}" != "succeeded" ]]; then
      echo "render retry timed out: ${RETRY_STATUS:-}" >&2
      exit 1
    fi
  fi
  if [[ "${EPICODE_SMOKE_CANCEL:-0}" == "1" ]]; then
    CANCEL_RENDER="$(api scene.render '{"format":"png","width":640,"height":360,"startFrame":0,"endFrame":240,"includeAudio":false}')"
    require_ok "${CANCEL_RENDER}"
    CANCEL_ID="$(printf '%s' "${CANCEL_RENDER}" | json_get result.jobId)"
    sleep 2
    CANCEL_RESULT="$(api render.cancel "{\"jobId\":\"${CANCEL_ID}\"}")"
    require_ok "${CANCEL_RESULT}"
    for _ in $(seq 1 30); do
      CANCEL_STATUS="$(api render.status "{\"jobId\":\"${CANCEL_ID}\"}")"
      CANCEL_STATE="$(printf '%s' "${CANCEL_STATUS}" | json_get result.state)"
      if [[ "${CANCEL_STATE}" == "cancelled" ]]; then
        echo "render cancel succeeded"
        break
      fi
      sleep 1
    done
    if [[ "${CANCEL_STATE:-}" != "cancelled" ]]; then
      echo "render cancel did not reach cancelled: ${CANCEL_STATUS:-}" >&2
      exit 1
    fi
  fi
fi

echo "Epicode smoke test passed"
