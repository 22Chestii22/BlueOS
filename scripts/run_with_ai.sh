#!/bin/bash
# BlueOS — run QEMU with Groq AI relay
# Starts the relay in background, then runs QEMU.
# Kill relay when QEMU exits.

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# Source .env for API key if present
if [ -f .env ]; then
    set -a
    . .env
    set +a
fi

echo "=== Starting Groq AI relay on port 8080 ==="
python3 scripts/groq_relay.py &
RELAY_PID=$!
echo "Relay PID: $RELAY_PID"

# Give relay a moment to start
sleep 1

cleanup() {
    echo "=== Stopping relay ==="
    kill $RELAY_PID 2>/dev/null || true
    wait $RELAY_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "=== Starting QEMU ==="
exec qemu-system-x86_64 -cdrom blueos.iso \
    -drive file=disk.img,format=raw,if=ide \
    -boot order=d -m 256M \
    -serial stdio -vga std \
    -nic model=rtl8139 \
    -usb -device usb-tablet
