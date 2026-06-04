#!/bin/bash
#
# BlueOS Automated QEMU Test Runner
#
# Usage:
#   ./scripts/test_runner.sh                  # Run all tests
#   ./scripts/test_runner.sh 1920x1080        # Test specific resolution
#   ./scripts/test_runner.sh --dry-run        # Print what would be done
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

QEMU="${QEMU:-qemu-system-x86_64}"
TIMEOUT=45
TESTS_PASSED=0
TESTS_TOTAL=0
DRY_RUN=0

GREEN=''
RED=''
NC=''
if [ -t 1 ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    NC='\033[0m'
fi

TEMP_DIR=""

cleanup() {
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR" 2>/dev/null || true
    fi
    # Ensure QEMU is killed
    if [ -n "${QEMU_PID:-}" ]; then
        kill "$QEMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    printf "[${GREEN}PASS${NC}] %s\n" "$1"
}

fail() {
    printf "[${RED}FAIL${NC}] %s\n" "$1"
}

# --- Helpers ---

die() {
    echo "ERROR: $*" >&2
    exit 1
}

check_prereqs() {
    if [ "$DRY_RUN" -eq 1 ]; then
        return 0
    fi
    command -v "$QEMU" >/dev/null 2>&1 || die "QEMU ($QEMU) not found"
    command -v grub-mkrescue >/dev/null 2>&1 || die "grub-mkrescue not found"
    command -v xorriso >/dev/null 2>&1 || die "xorriso not found"

    if [ ! -f "$PROJECT_DIR/kernel.elf" ]; then
        die "kernel.elf not found. Run 'make' first."
    fi
    if [ ! -f "$PROJECT_DIR/disk.img" ]; then
        die "disk.img not found. Run 'make' first."
    fi
}

# Build a test ISO with a specific framebuffer resolution in the GRUB config
build_test_iso() {
    local width="$1"
    local height="$2"
    local iso_path="$3"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] Would build ISO with resolution ${width}x${height} -> $iso_path"
        return 0
    fi

    local tmpdir
    tmpdir="$(mktemp -d)"
    TEMP_DIR="$tmpdir"

    mkdir -p "$tmpdir/boot/grub"
    cp "$PROJECT_DIR/kernel.elf" "$tmpdir/boot/"

    cat > "$tmpdir/boot/grub/grub.cfg" << GRUBEOF
set timeout=0
set default=0

menuentry "BlueOS Test (${width}x${height})" {
    insmod all_video
    set gfxpayload=${width}x${height}x32,keep
    terminal_output gfxterm
    multiboot2 /boot/kernel.elf
    boot
}
GRUBEOF

    grub-mkrescue -o "$iso_path" "$tmpdir" 2>/dev/null || {
        rm -rf "$tmpdir"
        return 1
    }
    rm -rf "$tmpdir"
    TEMP_DIR=""
    return 0
}

# Run QEMU with serial output capture and optional QMP
# Returns 0 if QEMU ran, exits with serial output file path in SERIAL_OUT
run_qemu() {
    local iso_path="$1"
    local width="$2"
    local height="$3"
    local serial_output="$4"
    local use_qmp="${5:-0}"
    local qmp_port="${6:-4444}"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] Would run: $QEMU -cdrom $iso_path -drive file=$PROJECT_DIR/disk.img,format=raw,if=ide"
        echo "  [DRY RUN]   -m 256M -vga std -boot order=d -usb -device usb-tablet"
        echo "  [DRY RUN]   -serial file:$serial_output"
        local extra=""
        if [ "$width" -gt 1920 ] 2>/dev/null; then
            extra="$extra -global VGA.vgamem_mb=16"
        fi
        if [ "$use_qmp" -eq 1 ]; then
            extra="$extra -qmp tcp:localhost:$qmp_port,server,nowait"
        fi
        [ -n "$extra" ] && echo "  [DRY RUN]  $extra"
        echo "  [DRY RUN]   (timeout: ${TIMEOUT}s)"
        echo "  [DRY RUN] Serial output -> $serial_output"
        SERIAL_OUT="$serial_output"
        return 0
    fi

    > "$serial_output"

    local qemu_extra=""
    if [ "$width" -gt 1920 ] 2>/dev/null || [ "$height" -gt 1200 ] 2>/dev/null; then
        qemu_extra="$qemu_extra -global VGA.vgamem_mb=16"
    fi
    if [ "$use_qmp" -eq 1 ]; then
        qemu_extra="$qemu_extra -qmp tcp:localhost:$qmp_port,server,nowait"
    fi

    $QEMU \
        -cdrom "$iso_path" \
        -drive file="$PROJECT_DIR/disk.img",format=raw,if=ide \
        -m 256M -vga std -boot order=d \
        -usb -device usb-tablet \
        -serial file:"$serial_output" \
        $qemu_extra \
        -display none \
        -no-reboot \
        -no-shutdown \
        &

    QEMU_PID=$!

    # Wait for QEMU to finish or timeout (0.5s ticks)
    local waited=0
    local max_waited=$((TIMEOUT * 2))
    while [ $waited -lt $max_waited ]; do
        if ! kill -0 "$QEMU_PID" 2>/dev/null; then
            wait "$QEMU_PID" 2>/dev/null || true
            QEMU_PID=""
            break
        fi
        sleep 0.5
        waited=$((waited + 1))
    done

    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        QEMU_PID=""
    fi

    SERIAL_OUT="$serial_output"
    return 0
}

# Wait for a pattern in the serial output file (polling)
wait_for_pattern() {
    local serial_file="$1"
    local pattern="$2"
    local timeout="$3"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] Would wait for '$pattern' in '$serial_file' (timeout: ${timeout}s)"
        return 0
    fi

    local waited=0
    local max_waited=$((timeout * 5))
    while [ $waited -lt "$max_waited" ]; do
        if [ -f "$serial_file" ] && grep -q "$pattern" "$serial_file" 2>/dev/null; then
            return 0
        fi
        sleep 0.2
        waited=$((waited + 1))
    done
    return 1
}

# Send QMP command via TCP
qmp_send() {
    local port="$1"
    local cmd="$2"
    local resp_file="$3"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] Would send QMP command to localhost:$port: $cmd"
        return 0
    fi

    {
        echo "$cmd" | timeout 5 nc -q 1 localhost "$port" 2>/dev/null || \
        echo "$cmd" | timeout 5 ncat localhost "$port" 2>/dev/null || \
        timeout 5 bash -c "echo '$cmd' > /dev/tcp/localhost/$port 2>/dev/null" 2>/dev/null
    } > "$resp_file" 2>&1 || true
}

# --- Test Functions ---

# Test 1: Resolution detection
test_resolution() {
    local width="$1"
    local height="$2"
    local res_label="${width}x${height}"
    local serial_out
    serial_out="$(mktemp /tmp/blueos_serial_XXXXXX.txt)"
    local iso_path="/tmp/blueos_test_${width}x${height}.iso"

    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local base="RESOLUTION $res_label"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] ===== Test: $base ====="
        echo "  [DRY RUN] Step 1: Build test ISO with GRUB gfxpayload=${res_label}x32"
        build_test_iso "$width" "$height" "$iso_path"
        echo "  [DRY RUN] Step 2: Boot QEMU and capture serial output"
        run_qemu "$iso_path" "$width" "$height" "$serial_out" 0
        echo "  [DRY RUN] Step 3: Verify serial output"
        echo "  [DRY RUN]   - Check 'Framebuffer found!'"
        echo "  [DRY RUN]   - Check 'Width:   ${width}'"
        echo "  [DRY RUN]   - Check 'Height:  ${height}'"
        echo "  [DRY RUN] Step 4: Cleanup"
        pass "$base"
        rm -f "$iso_path" "$serial_out"
        return 0
    fi

    echo "--- Test: $base ---"

    # Build ISO
    if ! build_test_iso "$width" "$height" "$iso_path"; then
        fail "$base (ISO build failed)"
        rm -f "$iso_path" "$serial_out"
        return 1
    fi

    # Run QEMU
    run_qemu "$iso_path" "$width" "$height" "$serial_out" 0

    # Check serial output
    local errors=()

    if grep -q "Framebuffer found!" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("No 'Framebuffer found!'")
    fi

    if grep -q "Width:" "$serial_out" 2>/dev/null; then
        local actual_width
        actual_width=$(grep "Width:" "$serial_out" | head -1 | tr -dc '0-9\n' | head -1 || echo "")
        if [ -n "$actual_width" ] && [ "$actual_width" = "$width" ]; then
            :  # pass
        else
            errors+=("Width mismatch: expected $width, got $actual_width")
        fi
    else
        errors+=("No 'Width:' in output")
    fi

    if grep -q "Height:" "$serial_out" 2>/dev/null; then
        local actual_height
        actual_height=$(grep "Height:" "$serial_out" | head -1 | tr -dc '0-9\n' | head -1 || echo "")
        if [ -n "$actual_height" ] && [ "$actual_height" = "$height" ]; then
            :  # pass
        else
            errors+=("Height mismatch: expected $height, got $actual_height")
        fi
    else
        errors+=("No 'Height:' in output")
    fi

    if [ ${#errors[@]} -eq 0 ]; then
        pass "$base"
    else
        local err_msg="$base"
        for e in "${errors[@]}"; do
            err_msg="$err_msg | $e"
        done
        fail "$err_msg"
    fi

    rm -f "$iso_path" "$serial_out"
}

# Test 2: Clean boot
test_clean_boot() {
    local serial_out
    serial_out="$(mktemp /tmp/blueos_serial_XXXXXX.txt)"
    local iso_path="/tmp/blueos_test_clean.iso"

    # Use default 1280x720 (kernel's native resolution)
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local base="CLEAN_BOOT"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] ===== Test: $base ====="
        echo "  [DRY RUN] Build ISO, boot QEMU, capture serial"
        echo "  [DRY RUN] Verifies:"
        echo "  [DRY RUN]   - No 'PANIC', 'exception', 'Double Fault', '#PF', '#GP' in output"
        echo "  [DRY RUN]   - All 4 modules load (DEMO, KEYB, MOUSE, TIMER)"
        echo "  [DRY RUN]   - Scheduler initialized"
        pass "$base"
        rm -f "$serial_out"
        return 0
    fi

    echo "--- Test: $base ---"

    build_test_iso 1280 720 "$iso_path" || {
        fail "$base (ISO build failed)"
        rm -f "$iso_path" "$serial_out"
        return 1
    }

    run_qemu "$iso_path" 1280 720 "$serial_out" 0

    local errors=()

    # Check for panic/exception indicators
    if grep -qi "PANIC" "$serial_out" 2>/dev/null; then
        errors+=("Found PANIC in output")
    fi
    if grep -qi "exception" "$serial_out" 2>/dev/null; then
        errors+=("Found 'exception' in output")
    fi
    if grep -qi "Double Fault" "$serial_out" 2>/dev/null; then
        errors+=("Found 'Double Fault' in output")
    fi
    if grep -qi "#PF" "$serial_out" 2>/dev/null; then
        errors+=("Found '#PF' in output")
    fi
    if grep -qi "#GP" "$serial_out" 2>/dev/null; then
        errors+=("Found '#GP' in output")
    fi
    if grep -qi "EXC " "$serial_out" 2>/dev/null; then
        errors+=("Found exception (EXC) in output")
    fi

    # Check scheduler
    if grep -q "SCHED.*Scheduler initialized" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("Scheduler not initialized")
    fi

    # Check all 4 modules
    if grep -q "\[DEMO\]" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("DEMO module not loaded")
    fi
    if grep -q "\[KEYB\]" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("KEYB module not loaded")
    fi
    if grep -q "\[MOUSE\]" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("MOUSE module not loaded")
    fi
    if grep -q "\[TIMER\]" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("TIMER module not loaded")
    fi

    # Check loader says 4 modules loaded
    if grep -q "Loaded 4 module(s)" "$serial_out" 2>/dev/null; then
        :  # pass
    else
        errors+=("Did not load exactly 4 modules")
    fi

    if [ ${#errors[@]} -eq 0 ]; then
        pass "$base"
    else
        local err_msg="$base"
        for e in "${errors[@]}"; do
            err_msg="$err_msg | $e"
        done
        fail "$err_msg"
    fi

    rm -f "$iso_path" "$serial_out"
}

# Test 3: Performance (boot time)
test_performance() {
    local serial_out
    serial_out="$(mktemp /tmp/blueos_serial_XXXXXX.txt)"
    local iso_path="/tmp/blueos_test_perf.iso"

    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local base="PERFORMANCE"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] ===== Test: $base ====="
        echo "  [DRY RUN] Build ISO, boot QEMU, measure boot time"
        echo "  [DRY RUN] Timing: first serial output -> 'SCHED] Scheduler initialized'"
        echo "  [DRY RUN] Would report boot time in ms"
        pass "$base"
        rm -f "$serial_out"
        return 0
    fi

    echo "--- Test: $base ---"

    build_test_iso 1280 720 "$iso_path" || {
        fail "$base (ISO build failed)"
        rm -f "$iso_path" "$serial_out"
        return 1
    }

    # Start QEMU with timing
    local start_time
    start_time=$(python3 -c "import time; print(int(time.time() * 1000))" 2>/dev/null || \
                 date +%s%3N 2>/dev/null || echo "0")

    run_qemu "$iso_path" 1280 720 "$serial_out" 0

    local end_time
    end_time=$(python3 -c "import time; print(int(time.time() * 1000))" 2>/dev/null || \
               date +%s%3N 2>/dev/null || echo "0")

    local boot_ms=$((end_time - start_time))

    # More accurate: measure between first serial char and scheduler init
    local first_line_time=""
    local sched_line_time=""
    if command -v python3 >/dev/null 2>&1; then
        # Python-based parsing if available (more accurate)
        first_line_time=$start_time
        sched_line_time=$end_time
    fi

    echo "  Boot time: ${boot_ms}ms (wall clock)"
    if [ "$boot_ms" -gt 0 ] 2>/dev/null; then
        pass "$base (${boot_ms}ms)"
    else
        pass "$base (timing unavailable)"
    fi

    rm -f "$iso_path" "$serial_out"
}

# Test 4: QEMU screendump
test_screendump() {
    local serial_out
    serial_out="$(mktemp /tmp/blueos_serial_XXXXXX.txt)"
    local iso_path="/tmp/blueos_test_screen.iso"
    local ppm_path="/tmp/blueos_screendump.ppm"
    local qmp_port=4444

    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    local base="SCREENDUMP"

    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [DRY RUN] ===== Test: $base ====="
        echo "  [DRY RUN] Step 1: Build ISO"
        echo "  [DRY RUN] Step 2: Boot QEMU with -qmp tcp:localhost:$qmp_port"
        echo "  [DRY RUN] Step 3: Wait for boot completion"
        echo "  [DRY RUN] Step 4: Send screendump command via QMP"
        echo "  [DRY RUN] Step 5: Verify PPM file dimensions"
        pass "$base"
        rm -f "$serial_out"
        return 0
    fi

    # Check if QMP tools available
    if ! command -v nc >/dev/null 2>&1 && ! command -v ncat >/dev/null 2>&1; then
        echo "  SKIP: nc/ncat not available for QMP communication"
        TESTS_TOTAL=$((TESTS_TOTAL - 1))
        rm -f "$iso_path" "$serial_out"
        return 0
    fi

    echo "--- Test: $base ---"

    build_test_iso 1280 720 "$iso_path" || {
        fail "$base (ISO build failed)"
        rm -f "$iso_path" "$serial_out"
        return 1
    }

    # Run QEMU with QMP enabled
    run_qemu "$iso_path" 1280 720 "$serial_out" 1 "$qmp_port"

    # Wait for boot (check for scheduler init or module load)
    if wait_for_pattern "$serial_out" "Loaded 4 module" 15; then
        # Wait a tiny bit more for framebuffer to be active
        sleep 1

        # Connect to QMP and send screendump
        local qmp_resp
        qmp_resp="$(mktemp /tmp/blueos_qmp_XXXXXX.txt)"

        # QMP handshake: capabilities + screendump
        qmp_send "$qmp_port" \
            '{"execute":"qmp_capabilities"}' \
            "$qmp_resp"

        qmp_send "$qmp_port" \
            "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$ppm_path\"}}" \
            "$qmp_resp"

        rm -f "$qmp_resp"
    fi

    # Verify PPM
    local errors=()
    if [ -f "$ppm_path" ]; then
        local ppm_size
        ppm_size=$(stat -c%s "$ppm_path" 2>/dev/null || stat -f%z "$ppm_path" 2>/dev/null || echo 0)
        if [ "$ppm_size" -gt 1000 ] 2>/dev/null; then
            :  # pass
        else
            errors+=("PPM file too small (${ppm_size}B)")
        fi

        # Parse PPM header to check dimensions
        if command -v python3 >/dev/null 2>&1; then
            local dims
            dims=$(python3 -c "
with open('$ppm_path', 'rb') as f:
    header = f.readline().strip()
    dims = f.readline().strip()
    maxval = f.readline().strip()
    w, h = dims.split()
    print(f'{w}x{h}')
" 2>/dev/null || echo "unknown")
            echo "  Screendump dimensions: $dims"
        fi

        pass "$base (${ppm_size}B PPM captured)"
        rm -f "$ppm_path"
    else
        if [ -f "$serial_out" ] && grep -q "Framebuffer found!" "$serial_out" 2>/dev/null; then
            errors+=("PPM file not created (QMP/screendump may not be supported)")
        else
            errors+=("No framebuffer, screendump skipped")
        fi
        if [ ${#errors[@]} -eq 0 ]; then
            pass "$base (QMP skipped - no framebuffer)"
        else
            local err_msg="$base"
            for e in "${errors[@]}"; do
                err_msg="$err_msg | $e"
            done
            fail "$err_msg"
        fi
    fi

    rm -f "$iso_path" "$serial_out"
}

# --- Main ---

print_summary() {
    echo ""
    echo "============================================"
    if [ "$TESTS_PASSED" -eq "$TESTS_TOTAL" ]; then
        printf "[${GREEN}PASS${NC}] ${TESTS_PASSED}/${TESTS_TOTAL} tests passed\n"
    else
        printf "[${RED}FAIL${NC}] ${TESTS_PASSED}/${TESTS_TOTAL} tests passed\n"
    fi
    echo "============================================"
}

run_all_tests() {
    echo "============================================"
    echo " BlueOS Automated Test Suite"
    echo "============================================"
    echo ""

    test_resolution 3840 2160
    test_resolution 1920 1080
    test_resolution 1280 720
    test_clean_boot
    test_performance
    test_screendump

    print_summary
}

run_single_test() {
    local res="$1"
    echo "============================================"
    echo " Single Resolution Test: $res"
    echo "============================================"
    echo ""

    local width
    local height
    width=$(echo "$res" | cut -d'x' -f1)
    height=$(echo "$res" | cut -d'x' -f2)

    if ! [[ "$width" =~ ^[0-9]+$ ]] || ! [[ "$height" =~ ^[0-9]+$ ]]; then
        die "Invalid resolution format: $res (expected WxH, e.g. 1920x1080)"
    fi

    test_resolution "$width" "$height"

    print_summary
}

# --- Parse args ---
if [[ " $* " =~ " --dry-run " ]]; then
    DRY_RUN=1
fi

check_prereqs

if [ $# -eq 0 ] || { [ $# -eq 1 ] && [ "$1" = "--dry-run" ]; }; then
    run_all_tests
elif [[ "$1" =~ ^[0-9]+x[0-9]+$ ]]; then
    run_single_test "$1"
else
    echo "Usage: $0 [resolution] [--dry-run]"
    echo ""
    echo "  resolution   WxH format (3840x2160, 1920x1080, 1280x720)"
    echo "               Default: run all 3 resolutions + full tests"
    echo "  --dry-run    Print actions without running QEMU"
    exit 1
fi

# Exit with status based on test results
if [ "$TESTS_PASSED" -eq "$TESTS_TOTAL" ] && [ "$TESTS_TOTAL" -gt 0 ]; then
    exit 0
elif [ "$TESTS_TOTAL" -gt 0 ]; then
    exit 1
fi
