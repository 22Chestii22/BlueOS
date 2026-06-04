#!/usr/bin/env python3
"""Test dir command output via VNC + QMP sendkey."""

import socket, subprocess, time, json, threading

QMP_PORT = 4448
LOG = "/tmp/blueos_dir.txt"


def qmp_connect():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(30)
    for attempt in range(30):
        try:
            sock.connect(("127.0.0.1", QMP_PORT))
            break
        except ConnectionRefusedError:
            time.sleep(0.5)
    else:
        raise Exception("Could not connect")
    sock.recv(4096)
    sock.send(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
    time.sleep(0.2)
    sock.recv(4096)
    return sock


def send_key(sock, key):
    req = (
        json.dumps(
            {
                "execute": "human-monitor-command",
                "arguments": {"command-line": f"sendkey {key}"},
            }
        )
        + "\n"
    )
    sock.send(req.encode())
    time.sleep(0.3)


log = open(LOG, "w")
print("[TEST] Starting QEMU with VNC...")
qemu = subprocess.Popen(
    [
        "qemu-system-x86_64",
        "-cdrom",
        "blueos.iso",
        "-drive",
        "file=disk.img,format=raw,if=ide",
        "-m",
        "256M",
        "-serial",
        "stdio",
        "-qmp",
        f"tcp:127.0.0.1:{QMP_PORT},server=on,wait=off",
        "-vga",
        "std",
        "-display",
        "vnc=:0",
        "-boot",
        "order=d",
        "-usb",
        "-device",
        "usb-tablet",
        "-k",
        "en-us",
        "-no-reboot",
        "-no-shutdown",
    ],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    bufsize=0,
)

lines = []
active = True


def capture():
    while active:
        line = qemu.stdout.readline()
        if line:
            decoded = line.decode(errors="replace").rstrip("\n\r")
            lines.append(decoded)
            log.write(decoded + "\n")
            log.flush()


thread = threading.Thread(target=capture, daemon=True)
thread.start()

time.sleep(3)
print("[TEST] Connecting to QMP...")
qmp = qmp_connect()
print("[TEST] Connected. Waiting for boot...")
time.sleep(18)  # Wait for boot + GUI + CMD startup

print("[TEST] Sending 'dir' command...")
for ch in "dir":
    send_key(qmp, ch)
    time.sleep(0.5)
send_key(qmp, "ret")
time.sleep(8)  # Wait for output

# Collect GUI: lines
gui_lines = [l for l in lines if "[GUI:" in l]
print(f"\n===== GUI OUTPUT LINES ({len(gui_lines)}) =====")
for l in gui_lines:
    print(l)

# Show last 80 serial lines
print(f"\n===== LAST 80 SERIAL LINES =====")
for l in lines[-80:]:
    print(l)

active = False
qmp.send(
    json.dumps(
        {"execute": "human-monitor-command", "arguments": {"command-line": "quit"}}
    ).encode()
    + b"\n"
)
time.sleep(2)
qemu.terminate()
qmp.close()
log.close()
print("[TEST] Done")
