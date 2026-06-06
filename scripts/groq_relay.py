#!/usr/bin/env python3
"""
Groq API relay for BlueOS.
Listens on port 8080, forwards POST /v1/chat/completions to api.groq.com (HTTPS).
BlueOS kernel connects to 10.0.2.2:8080 (QEMU host) to use this relay.

Usage:
  python3 scripts/groq_relay.py [--port 8080]

Requires: GROQ_API_KEY environment variable (e.g. export GROQ_API_KEY='gsk_...').
"""

import json
import os
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.request import Request, urlopen
from urllib.error import URLError

API_KEY = os.environ.get("GROQ_API_KEY", "")
GROQ_URL = "https://api.groq.com/openai/v1/chat/completions"


class RelayHandler(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()

    def do_POST(self):
        content_len = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_len)

        groq_headers = {
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json",
        }

        try:
            req = Request(GROQ_URL, data=body, headers=groq_headers, method="POST")
            resp = urlopen(req, timeout=30)
            resp_body = resp.read()

            self.send_response(resp.status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp_body)))
            self.end_headers()
            self.wfile.write(resp_body)

            print(f"[OK] {self.path} -> {resp.status} ({len(resp_body)} bytes)")
        except URLError as e:
            error = json.dumps({"error": str(e.reason)}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            print(f"[ERR] {self.path} -> {e.reason}")
        except Exception as e:
            error = json.dumps({"error": str(e)}).encode()
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            print(f"[ERR] {self.path} -> {e}")

    def log_message(self, format, *args):
        print(f"[REQ] {args[0]} {args[1]} {args[2]}")


def main():
    port = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == "--port" else 8080
    server = HTTPServer(("0.0.0.0", port), RelayHandler)
    print(f"Groq relay listening on 0.0.0.0:{port}")
    print(f"Forwarding to {GROQ_URL}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.server_close()


if __name__ == "__main__":
    main()
