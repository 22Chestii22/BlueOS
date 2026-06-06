#!/usr/bin/env python3
"""
Groq API relay for BlueOS.
Listens on port 8080:
  POST /v1/chat/completions  — forwards to api.groq.com (HTTPS)
  POST /generate             — generates a BlueOS .blu program from a text query

BlueOS kernel connects to 10.0.2.2:8080 (QEMU host) to use this relay.

Usage:
  python3 scripts/groq_relay.py [--port 8080]

Requires: GROQ_API_KEY environment variable (e.g. export GROQ_API_KEY='gsk_...').
"""

import json
import os
import re
import sys
import subprocess
import tempfile
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.request import Request, urlopen
from urllib.error import URLError


PROJECT_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))


def load_env(path=".env"):
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                if line.startswith("export "):
                    line = line[7:]
                k, _, v = line.partition("=")
                k = k.strip()
                v = v.strip().strip("'\"")
                if k and v:
                    os.environ.setdefault(k, v)
    except FileNotFoundError:
        pass


load_env()

API_KEY = os.environ.get("GROQ_API_KEY", "")
GROQ_URL = "https://api.groq.com/openai/v1/chat/completions"


BLUEOS_SYSTEM_PROMPT = """\
You are a BlueOS program generator for a hobby x86-64 operating system.
Generate a complete, working x86-64 NASM assembly program for BlueOS.

CRITICAL NASM RULES (violations cause compilation failure):
- NEVER use dot-prefixed local labels (.label) — use ONLY unique global labels like label_1, print_done, loop_start, etc.
- NEVER reuse a label name — every label must be unique
- Use BITS 64
- Each line must be self-contained (no label context dependencies)

PROGRAM STRUCTURE:
- First line MUST be: ; NAME: ProgramName (alpha characters only, max 8 chars)
- Second line: ; DESC: One-line description
- Entry point is the first instruction after BITS 64 (labeled 'start:')
- Image base is 0x400000, entry RVA is 0
- End with syscall 1 (exit) followed by infinite loop: jmp $

AVAILABLE SYSCALLS (rax = number):
  1 = exit(rdi=exit_code)
  2 = getpid() returns pid in rax
  3 = print(rdi=string_address) — prints null-terminated string
  4 = malloc(rdi=size) returns pointer in rax
  5 = free(rdi=pointer)
  13 = sleep(rdi=milliseconds)
  28 = yield()

FLAT BINARY CONSTRAINTS:
- DO NOT use sections (.text, .data) — flat binary only
- DO NOT use BSS — use 'times N db 0' instead
- All strings must use db directives inline with code
- Use 'lea rdi, [rel label]' for RIP-relative string addressing
- Must compile with: nasm -f bin
- Keep it under 80 lines

GOOD LABEL EXAMPLE (DO THIS):
start:
    mov rax, 3
    lea rdi, [rel msg_welcome]
    syscall
    mov rax, 1
    xor rdi, rdi
    syscall
    jmp $
msg_welcome: db "Hello!", 0xD, 0xA, 0

Write a creative, fun program that does something interesting.
"""


def call_groq(
    system_prompt, user_message, model="llama-3.1-8b-instant", max_tokens=2048
):
    body = json.dumps(
        {
            "model": model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_message},
            ],
            "max_tokens": max_tokens,
        }
    ).encode()

    req = Request(
        GROQ_URL,
        data=body,
        headers={
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json",
            "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
        },
        method="POST",
    )
    resp = urlopen(req, timeout=60)
    resp_body = resp.read()
    data = json.loads(resp_body)
    content = data["choices"][0]["message"]["content"]
    return content


def sanitize_name(name):
    name = re.sub(r"[^a-zA-Z0-9]", "", name)
    if not name:
        name = "App"
    return name[:8].upper()


def generate_blu(query):
    max_attempts = 3
    last_error = ""
    raw = ""

    for attempt in range(max_attempts):
        if attempt == 0:
            system_prompt = BLUEOS_SYSTEM_PROMPT
            user_message = query
        else:
            system_prompt = (
                BLUEOS_SYSTEM_PROMPT
                + "\n\nYour previous attempt had a compilation error. Fix it:\n"
                + last_error
            )
            user_message = (
                query
                + "\n\nFix the NASM compilation error above and regenerate the full program."
            )

        raw = call_groq(system_prompt, user_message)
        raw = raw.strip()

        name_match = re.search(r"; NAME:\s*(\S+)", raw)
        prog_name = name_match.group(1) if name_match else "AIApp"
        prog_name = sanitize_name(prog_name)

        desc_match = re.search(r"; DESC:\s*(.+)", raw)
        description = desc_match.group(1).strip() if desc_match else ""

        raw = re.sub(r"^```[a-zA-Z]*\s*\n?", "", raw)
        raw = re.sub(r"\n?```\s*$", "", raw)

        code_start = re.search(r"BITS\s+64", raw, re.IGNORECASE)
        if code_start:
            raw = raw[code_start.start() :]
        else:
            lines = [
                l
                for l in raw.split("\n")
                if not l.strip().startswith("; NAME:")
                and not l.strip().startswith("; DESC:")
            ]
            raw = "\n".join(lines)

        asm_dir = os.path.join(PROJECT_ROOT, "build", "gen")
        os.makedirs(asm_dir, exist_ok=True)

        asm_path = os.path.join(asm_dir, f"{prog_name}.asm")
        bin_path = os.path.join(asm_dir, f"{prog_name}.bin")
        blu_path = os.path.join(asm_dir, f"{prog_name}.blu")

        lines = raw.split("\n")
        fixed_lines = []
        for line in lines:
            stripped = line.strip()
            if stripped.startswith(".") and ":" in stripped:
                label = stripped.split(":")[0]
                fixed_lines.append(line.replace(label, label.replace(".", "L_")))
            else:
                fixed_lines.append(line)
        raw = "\n".join(fixed_lines)

        with open(asm_path, "w") as f:
            f.write(raw)

        result = subprocess.run(
            ["nasm", "-f", "bin", "-o", bin_path, asm_path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            last_error = result.stderr.strip() or result.stdout.strip()
            print(
                f"[NASM] Attempt {attempt + 1}/{max_attempts} failed: {last_error[:100]}"
            )
            continue

        result = subprocess.run(
            [
                sys.executable,
                "scripts/make_blu.py",
                bin_path,
                blu_path,
                "0x400000",
                "0",
                prog_name,
            ],
            capture_output=True,
            text=True,
            timeout=30,
            cwd=PROJECT_ROOT,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"make_blu failed: {result.stderr.strip() or result.stdout.strip()}"
            )

        disk_img = os.path.join(PROJECT_ROOT, "disk.img")
        if not os.path.exists(disk_img):
            raise RuntimeError(
                "disk.img not found — build it first with 'make disk.img'"
            )

        subprocess.run(
            ["mmd", "-i", disk_img, "::/SYSTEM/AI"],
            capture_output=True,
            timeout=30,
        )

        dest_path = f"::/SYSTEM/AI/{prog_name}.BLU"
        result = subprocess.run(
            ["mcopy", "-i", disk_img, blu_path, dest_path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"mcopy failed: {result.stderr.strip() or result.stdout.strip()}"
            )

        print(f"[GEN] {prog_name}: {query[:40]}... -> {dest_path}")

        return {
            "path": f"\\SYSTEM\\AI\\{prog_name}.BLU",
            "name": prog_name,
            "description": description,
        }

    raise RuntimeError(f"NASM failed after {max_attempts} attempts: {last_error}")


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

        try:
            if self.path == "/generate":
                self.handle_generate(body)
            else:
                self.handle_groq_proxy(body)
        except Exception as e:
            error = json.dumps({"status": "error", "message": str(e)}).encode()
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            print(f"[ERR] {self.path} -> {e}")

    def handle_generate(self, body):
        try:
            data = json.loads(body)
        except json.JSONDecodeError as e:
            error = json.dumps(
                {"status": "error", "message": f"Invalid JSON: {e}"}
            ).encode()
            self.send_response(400)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            return

        query = data.get("query", "")
        if not query or not query.strip():
            error = json.dumps(
                {"status": "error", "message": "Missing 'query' field"}
            ).encode()
            self.send_response(400)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            return

        if not API_KEY:
            error = json.dumps(
                {"status": "error", "message": "GROQ_API_KEY not set"}
            ).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            return

        try:
            result = generate_blu(query.strip())
            resp_body = json.dumps({"status": "ok", **result}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp_body)))
            self.end_headers()
            self.wfile.write(resp_body)
            print(f"[GEN] OK -> {result['path']}")
        except Exception as e:
            error = json.dumps({"status": "error", "message": str(e)}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            print(f"[GEN] FAIL -> {e}")

    def handle_groq_proxy(self, body):
        if not API_KEY:
            error = json.dumps({"error": "GROQ_API_KEY not set"}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error)))
            self.end_headers()
            self.wfile.write(error)
            return

        groq_headers = {
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json",
            "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
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

    def log_message(self, format, *args):
        print(f"[REQ] {args[0]} {args[1]} {args[2]}")


def main():
    port = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == "--port" else 8080
    server = HTTPServer(("0.0.0.0", port), RelayHandler)
    print(f"Groq relay listening on 0.0.0.0:{port}")
    print(f"Forwarding to {GROQ_URL}")
    print(f"Project root: {PROJECT_ROOT}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.server_close()


if __name__ == "__main__":
    main()
