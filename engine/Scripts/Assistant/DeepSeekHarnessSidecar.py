#!/usr/bin/env python3
"""Epicode DeepSeek Harness sidecar.

The editor talks to this process over newline-delimited JSON on stdin/stdout.
The process owns the isolated Python environment and the SDK runtime, so a
crash or timeout only affects the conversation panel.

Protocol (one JSON object per line, UTF-8, <= 1 MiB):
  in : {"id":"1","op":"chat","prompt":"...","context":"...","history":[...]}
  in : {"id":"1","op":"cancel"}
  in : {"op":"ping"}
  in : {"op":"shutdown"}
  out: {"id":"1","event":"delta","text":"..."}
  out: {"id":"1","event":"done","finishReason":"..."}
  out: {"id":"1","event":"cancelled"}
  out: {"id":"1","event":"error","code":"...","message":"..."}
  out: {"event":"pong"}
  out: {"event":"idle_exit"}
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import secrets
import select
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
from urllib.parse import urlparse

MAX_LINE_BYTES = 1024 * 1024
MAX_PROMPT_CHARS = 200_000
_emit_lock = threading.Lock()


def _emit(payload: dict[str, Any]) -> None:
    line = json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n"
    try:
        with _emit_lock:
            sys.stdout.write(line)
            sys.stdout.flush()
    except (BrokenPipeError, OSError):
        os._exit(0)


def _log(message: str) -> None:
    print(f"[epicode-assistant] {message}", file=sys.stderr, flush=True)


def _default_project_dir() -> Path:
    # engine/Scripts/Assistant/DeepSeekHarnessSidecar.py -> UE project dir (engine/)
    return Path(__file__).resolve().parents[2]


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Epicode DeepSeek Harness sidecar")
    parser.add_argument("--project-dir", default=str(_default_project_dir()))
    parser.add_argument("--model", default=os.environ.get("EPICODE_ASSISTANT_MODEL", "deepseek-chat"))
    parser.add_argument("--base-url", default=os.environ.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com"))
    parser.add_argument("--api-key-file", default=None)
    parser.add_argument("--dsh-home", default=None)
    parser.add_argument("--venv", default=None)
    parser.add_argument("--idle-seconds", type=int, default=300)
    parser.add_argument("--request-timeout-seconds", type=float, default=120.0)
    parser.add_argument("--mode", choices=("stdio", "http"), default="stdio")
    parser.add_argument("--http-host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8720)
    parser.add_argument("--http-token-file", default=None)
    parser.add_argument("--no-reexec", action="store_true")
    return parser


def _maybe_reexec(args: argparse.Namespace) -> None:
    if args.no_reexec or os.environ.get("EPICODE_ASSISTANT_VENV_ACTIVE") == "1":
        return

    project_dir = Path(args.project_dir).resolve()
    venv_dir = Path(args.venv).resolve() if args.venv else project_dir / "Saved/Epicode/Assistant/venv"
    venv_python = venv_dir / "bin/python"
    if not venv_python.is_file():
        return
    if Path(sys.prefix).resolve() == venv_dir:
        os.environ["EPICODE_ASSISTANT_VENV_ACTIVE"] = "1"
        return

    env = os.environ.copy()
    env["EPICODE_ASSISTANT_VENV_ACTIVE"] = "1"
    _log(f"reexec into {venv_python}")
    os.execve(str(venv_python), [str(venv_python), str(Path(__file__).resolve()), *sys.argv[1:]], env)


def _read_api_key(path: Path) -> str:
    environment_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if environment_key:
        return environment_key

    try:
        contents = path.read_text(encoding="utf-8")
    except OSError:
        return ""

    for raw_line in contents.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].strip()
        name = ""
        value = line
        if "=" in line:
            name, value = line.split("=", 1)
            name = name.strip()
            if name not in ("DEEPSEEK_API_KEY", "EPICODE_DEEPSEEK_API_KEY"):
                continue
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        if value:
            return value
    return ""


def _content_text(content: Any) -> str:
    if isinstance(content, str):
        return content
    if not isinstance(content, list):
        return ""
    parts: list[str] = []
    for block in content:
        if isinstance(block, dict) and block.get("type") == "text":
            parts.append(str(block.get("text") or ""))
    return "".join(parts)


def _notification_text(notification: Any) -> tuple[str, str]:
    """Return (message id, text) for an assistant message notification."""
    if getattr(notification, "method", "") != "session.event":
        return "", ""
    payload = getattr(notification, "payload", None)
    if not isinstance(payload, dict):
        return "", ""
    event = payload.get("event")
    if not isinstance(event, dict):
        return "", ""
    event_type = str(event.get("type") or "")
    if not event_type.startswith("assistant/"):
        return "", ""

    data = event.get("data")
    if not isinstance(data, dict):
        return "", ""
    if event_type.endswith("/delta"):
        text = data.get("text")
        if not isinstance(text, str):
            delta = data.get("delta")
            text = delta if isinstance(delta, str) else ""
        return "", text

    if event_type != "assistant/message":
        return "", ""

    message = data.get("message")
    owner = message if isinstance(message, dict) else data
    message_id = str(owner.get("id") or owner.get("messageId") or data.get("messageId") or "")
    text = _content_text(owner.get("content"))
    if not text:
        text = _content_text(data.get("content"))
    return message_id, text


def _compose_prompt(payload: dict[str, Any]) -> str:
    prompt = str(payload.get("prompt") or "").strip()
    context = str(payload.get("context") or "").strip()
    history = payload.get("history")
    messages = payload.get("messages")

    parts: list[str] = []
    if context:
        parts.append("Epicode 场景上下文：\n" + context)

    history_lines: list[str] = []
    if isinstance(history, list):
        for item in history:
            if not isinstance(item, dict):
                continue
            role = str(item.get("role") or item.get("Role") or "user")
            content = str(item.get("content") or item.get("Content") or "").strip()
            if content:
                history_lines.append(f"{role}: {content}")
    elif isinstance(messages, list):
        for item in messages:
            if not isinstance(item, dict):
                continue
            role = str(item.get("role") or "user")
            content = str(item.get("content") or "").strip()
            if content:
                history_lines.append(f"{role}: {content}")

    if history_lines:
        parts.append("最近对话：\n" + "\n".join(history_lines[-20:]))
    if prompt:
        parts.append("用户请求：\n" + prompt)
    return "\n\n".join(parts)[:MAX_PROMPT_CHARS]


def _safe_session_id(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_-]", "-", value).strip("-")
    return ("epicode-" + cleaned)[:96] if cleaned else "epicode-session"


class HarnessRunner:
    def __init__(
        self,
        args: argparse.Namespace,
        emit: Callable[[dict[str, Any]], None] = _emit,
    ) -> None:
        self.args = args
        self._emit = emit
        self._lock = threading.Lock()
        self._busy = False
        self._cancel = threading.Event()
        self._harness: Any = None
        self._project_dir = Path(args.project_dir).resolve()
        self._api_key_file = (
            Path(args.api_key_file).resolve()
            if args.api_key_file
            else self._project_dir / "Saved/Epicode/deepseek.env"
        )
        self._dsh_home = (
            Path(args.dsh_home).resolve()
            if args.dsh_home
            else self._project_dir / "Saved/Epicode/Assistant/dsh-home"
        )

    @property
    def busy(self) -> bool:
        with self._lock:
            return self._busy

    def cancel(self) -> None:
        self._cancel.set()
        with self._lock:
            harness = self._harness
        if harness is not None:
            threading.Thread(target=self._safe_close, args=(harness,), daemon=True).start()

    @staticmethod
    def _safe_close(harness: Any) -> None:
        try:
            harness.close()
        except Exception:
            pass

    def close(self) -> None:
        self.cancel()

    def run(self, request_id: str, payload: dict[str, Any]) -> None:
        with self._lock:
            if self._busy:
                self._emit({"id": request_id, "event": "error", "code": "busy", "message": "sidecar 正忙"})
                return
            self._busy = True
            self._cancel.clear()

        try:
            self._run_inner(request_id, payload)
        finally:
            with self._lock:
                self._busy = False
                self._harness = None

    def _run_inner(self, request_id: str, payload: dict[str, Any]) -> None:
        prompt = _compose_prompt(payload)
        if not prompt:
            self._emit({"id": request_id, "event": "error", "code": "empty_prompt", "message": "对话内容为空"})
            return

        api_key = _read_api_key(self._api_key_file)
        if len(api_key) < 8:
            self._emit({
                "id": request_id,
                "event": "error",
                "code": "missing_api_key",
                "message": f"未找到 DEEPSEEK_API_KEY，也未在 {self._api_key_file} 中找到有效 Key",
            })
            return

        try:
            from deepseek_harness import DeepSeekHarness, DeepSeekHarnessConfig
        except ImportError as exc:
            self._emit({
                "id": request_id,
                "event": "error",
                "code": "sdk_missing",
                "message": f"deepseek-harness-sdk 未安装：{exc}。运行 Scripts/Assistant/SetupSidecar.sh。",
            })
            return

        self._dsh_home.mkdir(parents=True, exist_ok=True)
        emitted_message_ids: set[str] = set()
        emitted_text = False

        def on_notification(notification: Any) -> None:
            nonlocal emitted_text
            message_id, text = _notification_text(notification)
            if not text:
                return
            if message_id:
                if message_id in emitted_message_ids:
                    return
                emitted_message_ids.add(message_id)
            emitted_text = True
            self._emit({"id": request_id, "event": "delta", "text": text})

        config_kwargs: dict[str, Any] = {
            "provider": "deepseek-official",
            "model": self.args.model,
            "cwd": str(self._project_dir),
            "runtime_cwd": str(self._project_dir),
            "dsh_home": str(self._dsh_home),
            "api_key": api_key,
            "request_timeout_seconds": max(5.0, float(self.args.request_timeout_seconds)),
        }
        if self.args.base_url:
            config_kwargs["base_url"] = self.args.base_url

        harness = None
        try:
            harness = DeepSeekHarness(DeepSeekHarnessConfig(**config_kwargs))
            with self._lock:
                self._harness = harness
            session_id = _safe_session_id(
                str(payload.get("conversationId") or payload.get("conversation_id") or request_id)
            )
            result = harness.run(prompt, session_id=session_id, on_notification=on_notification)
            if self._cancel.is_set():
                self._emit({"id": request_id, "event": "cancelled"})
                return
            final_text = getattr(result, "final_response", "") or ""
            finish_reason = getattr(result, "finish_reason", None) or ""
            if finish_reason == "error" and not emitted_text:
                self._emit({
                    "id": request_id,
                    "event": "error",
                    "code": "runtime_error",
                    "message": final_text or "DeepSeek Harness 运行失败",
                })
                return
            if final_text and not emitted_text:
                self._emit({"id": request_id, "event": "delta", "text": final_text})
            self._emit({
                "id": request_id,
                "event": "done",
                "finishReason": finish_reason,
            })
        except BaseException as exc:
            if self._cancel.is_set():
                self._emit({"id": request_id, "event": "cancelled"})
            else:
                message = str(exc).replace(api_key, "[redacted]")
                _log(f"chat {request_id} failed: {type(exc).__name__}: {message}")
                self._emit({
                    "id": request_id,
                    "event": "error",
                    "code": "runtime_error",
                    "message": f"{type(exc).__name__}: {message}",
                })
        finally:
            if harness is not None:
                self._safe_close(harness)


def _read_line(stream: Any, timeout: float) -> bytes | None:
    if os.name == "posix" and timeout >= 0:
        ready, _, _ = select.select([stream], [], [], timeout)
        if not ready:
            return None
    return stream.readline()


def _serve_stdio(args: argparse.Namespace) -> int:
    runner = HarnessRunner(args)
    idle_seconds = max(30, int(args.idle_seconds))
    shutdown = False
    last_activity = time.monotonic()

    while not shutdown:
        remaining = max(0.0, idle_seconds - (time.monotonic() - last_activity))
        line = _read_line(sys.stdin.buffer, remaining)
        if line is None:
            if runner.busy:
                last_activity = time.monotonic()
                continue
            _emit({"event": "idle_exit"})
            break
        if not line:
            if runner.busy:
                time.sleep(0.05)
                continue
            break
        last_activity = time.monotonic()

        if len(line) > MAX_LINE_BYTES:
            _emit({"event": "error", "code": "line_too_long", "message": "sidecar 请求超过 1 MiB"})
            continue
        try:
            payload = json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            _emit({"event": "error", "code": "bad_json", "message": f"无法解析 JSON: {exc}"})
            continue
        if not isinstance(payload, dict):
            _emit({"event": "error", "code": "bad_request", "message": "请求必须是 JSON 对象"})
            continue

        request_id = str(payload.get("id") or "")
        op = str(payload.get("op") or "")
        if op == "ping":
            _emit({"id": request_id, "event": "pong"})
        elif op == "shutdown":
            runner.close()
            _emit({"id": request_id, "event": "done"})
            shutdown = True
        elif op == "cancel":
            runner.cancel()
        elif op == "chat":
            if runner.busy:
                _emit({"id": request_id, "event": "error", "code": "busy", "message": "sidecar 正忙"})
                continue
            threading.Thread(
                target=runner.run,
                args=(request_id, payload),
                daemon=True,
                name=f"epicode-chat-{request_id}",
            ).start()
        else:
            _emit({
                "id": request_id,
                "event": "error",
                "code": "unknown_op",
                "message": f"未知操作：{op}",
            })

    runner.close()
    return 0


def _load_http_token(args: argparse.Namespace) -> tuple[str, Path]:
    environment_token = os.environ.get("EPICODE_ASSISTANT_TOKEN", "").strip()
    if environment_token:
        return environment_token, Path()

    project_dir = Path(args.project_dir).resolve()
    token_path = (
        Path(args.http_token_file).resolve()
        if args.http_token_file
        else project_dir / "Saved/Epicode/assistant-token"
    )
    try:
        existing = token_path.read_text(encoding="utf-8").strip()
        if existing:
            return existing, token_path
    except OSError:
        pass

    token = secrets.token_urlsafe(32)
    token_path.parent.mkdir(parents=True, exist_ok=True)
    token_path.write_text(token + "\n", encoding="utf-8")
    os.chmod(token_path, 0o600)
    _log(f"created local HTTP token at {token_path}")
    return token, token_path


def _serve_http(args: argparse.Namespace) -> int:
    token, token_path = _load_http_token(args)
    runners: dict[str, HarnessRunner] = {}
    runners_lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        server_version = "EpicodeAssistant/1.0"

        def log_message(self, fmt: str, *values: Any) -> None:
            _log(f"http {self.address_string()} {fmt % values}")

        def _authorized(self) -> bool:
            header = self.headers.get("Authorization", "").strip()
            direct = self.headers.get("X-Epicode-Token", "").strip()
            return direct == token or header == f"Bearer {token}"

        def _send_json(self, status: int, payload: dict[str, Any]) -> None:
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def _read_json(self) -> dict[str, Any] | None:
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                return None
            if length <= 0 or length > MAX_LINE_BYTES:
                return None
            try:
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                return None
            return payload if isinstance(payload, dict) else None

        def do_GET(self) -> None:
            if not self._authorized():
                self._send_json(401, {"error": "unauthorized"})
                return
            if urlparse(self.path).path == "/health":
                self._send_json(200, {
                    "status": "ok",
                    "backend": "deepseek_sidecar",
                    "mode": "http",
                })
                return
            self._send_json(404, {"error": "not_found"})

        def do_POST(self) -> None:
            if not self._authorized():
                self._send_json(401, {"error": "unauthorized"})
                return
            path = urlparse(self.path).path
            payload = self._read_json()
            if payload is None:
                self._send_json(400, {"error": "bad_json"})
                return

            if path == "/v1/cancel":
                request_id = str(payload.get("id") or "")
                with runners_lock:
                    runner = runners.get(request_id)
                if runner is not None:
                    runner.cancel()
                self._send_json(200, {"ok": runner is not None, "id": request_id})
                return

            if path != "/v1/chat":
                self._send_json(404, {"error": "not_found"})
                return

            request_id = str(payload.get("id") or f"http-{uuid.uuid4().hex}")
            events: queue.Queue[dict[str, Any] | None] = queue.Queue()
            runner = HarnessRunner(args, emit=events.put)
            with runners_lock:
                if request_id in runners:
                    self._send_json(409, {"error": "duplicate_request_id"})
                    return
                runners[request_id] = runner

            def run_chat() -> None:
                try:
                    runner.run(request_id, payload)
                finally:
                    events.put(None)

            threading.Thread(
                target=run_chat,
                daemon=True,
                name=f"epicode-http-chat-{request_id}",
            ).start()

            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                while True:
                    event = events.get(timeout=max(1.0, float(args.request_timeout_seconds) + 10.0))
                    if event is None:
                        break
                    data = json.dumps(event, ensure_ascii=False, separators=(",", ":"))
                    self.wfile.write(f"data: {data}\n\n".encode("utf-8"))
                    self.wfile.flush()
                    if event.get("event") in ("done", "error", "cancelled"):
                        break
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                runner.cancel()
            finally:
                with runners_lock:
                    runners.pop(request_id, None)

    try:
        server = ThreadingHTTPServer((args.http_host, args.http_port), Handler)
    except OSError as exc:
        _log(f"cannot bind {args.http_host}:{args.http_port}: {exc}")
        return 1
    server.daemon_threads = True
    _log(f"HTTP mode listening on {args.http_host}:{args.http_port} (token: {token_path or 'environment'})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


def main() -> int:
    args = _build_parser().parse_args()
    _maybe_reexec(args)
    project_dir = Path(args.project_dir).resolve()
    os.makedirs(project_dir / "Saved/Epicode/Assistant", exist_ok=True)
    if args.mode == "http":
        return _serve_http(args)
    return _serve_stdio(args)


if __name__ == "__main__":
    raise SystemExit(main())
