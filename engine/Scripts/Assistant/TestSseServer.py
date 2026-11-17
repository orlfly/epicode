#!/usr/bin/env python3
"""Minimal local SSE server used by Epicode.Assistant.HttpBackend."""
import json
import socketserver
import sys
from http.server import BaseHTTPRequestHandler

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18721


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length:
            self.rfile.read(length)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        for text in ("你", "好"):
            payload = {"choices": [{"delta": {"content": text}}]}
            self.wfile.write(
                ("data: " + json.dumps(payload, ensure_ascii=False) + "\n\n").encode("utf-8")
            )
            self.wfile.flush()
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def log_message(self, *_args):
        pass


class Server(socketserver.TCPServer):
    allow_reuse_address = True


with Server(("127.0.0.1", PORT), Handler) as httpd:
    print("READY", flush=True)
    httpd.serve_forever()
