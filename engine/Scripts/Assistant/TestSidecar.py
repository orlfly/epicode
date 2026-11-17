#!/usr/bin/env python3
"""Deterministic sidecar fixture for Epicode automation tests.

It implements the same JSONL protocol as DeepSeekHarnessSidecar.py but never
imports the SDK or contacts the network. Tests select behavior with prompt
markers: crash, timeout, or cancel.
"""

from __future__ import annotations

import json
import sys
import time


def emit(payload: dict) -> None:
    sys.stdout.write(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request = json.loads(line)
        request_id = str(request.get("id") or "")
        op = str(request.get("op") or "")
        if op == "ping":
            emit({"id": request_id, "event": "pong"})
        elif op == "shutdown":
            emit({"id": request_id, "event": "done"})
            return 0
        elif op == "cancel":
            emit({"id": request_id, "event": "cancelled"})
        elif op == "chat":
            prompt = str(request.get("prompt") or "")
            if "crash" in prompt:
                raise SystemExit(3)
            if "timeout" in prompt:
                time.sleep(3)
                emit({"id": request_id, "event": "done"})
                continue
            if "cancel" in prompt:
                emit({"id": request_id, "event": "delta", "text": "第一段"})
                time.sleep(3)
                emit({"id": request_id, "event": "done"})
                continue
            emit({"id": request_id, "event": "delta", "text": "你好"})
            emit({"id": request_id, "event": "delta", "text": "，世界"})
            emit({"id": request_id, "event": "done"})
        else:
            emit({"id": request_id, "event": "error", "code": "unknown_op", "message": op})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
