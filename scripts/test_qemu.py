#!/usr/bin/env python3
"""
BlueOS Automated QEMU Testing Framework.
Runs BlueOS under QEMU and validates boot output via serial.
"""

import argparse
import subprocess
import sys
import os
import time
import signal
import re
import tempfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TIMEOUT = 30


def build():
    print("=== Building BlueOS ===")
    result = subprocess.run(
        ["make", "-j$(nproc)"],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        shell=True,
    )
    if result.returncode != 0:
        print("BUILD FAILED")
        print(result.stdout)
        print(result.stderr)
        return False
    print("Build OK")
    return True


def run_qemu(expect_patterns, timeout=DEFAULT_TIMEOUT, resolution=None, extra_args=""):
    serial_file = tempfile.mktemp(suffix=".txt", prefix="blueos_test_")

    qemu_args = (
        f"-cdrom {PROJECT_ROOT}/blueos.iso "
        f"-drive file={PROJECT_ROOT}/disk.img,format=raw,if=ide "
        f"-boot order=d -usb -device usb-tablet -m 256M "
        f"-serial file:{serial_file} -vga std "
        f"-display none -no-reboot -no-shutdown "
        f"{extra_args}"
    )

    if resolution == "4k":
        qemu_args += " -global VGA.vgamem_mb=64 -m 512M"

    cmd = f"qemu-system-x86_64 {qemu_args}"

    print(f"  QEMU: {cmd}")

    proc = subprocess.Popen(
        cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )

    try:
        start = time.time()
        matched = []
        remaining = list(expect_patterns)

        while time.time() - start < timeout:
            proc.poll()
            if proc.returncode is not None:
                break

            try:
                with open(serial_file, "r", errors="replace") as f:
                    output = f.read()
            except (FileNotFoundError, IOError):
                output = ""

            new_remaining = []
            for pattern in remaining:
                if re.search(pattern, output):
                    matched.append(pattern)
                    print(f"    MATCH: {pattern}")
                else:
                    new_remaining.append(pattern)
            remaining = new_remaining

            if not remaining:
                break

            time.sleep(0.1)

        try:
            with open(serial_file, "r", errors="replace") as f:
                output = f.read()
        except (FileNotFoundError, IOError):
            output = ""

        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

        success = len(matched) == len(expect_patterns)

        if success:
            print(f"  PASS (matched {len(matched)}/{len(expect_patterns)} patterns)")
        else:
            print(f"  FAIL (matched {len(matched)}/{len(expect_patterns)} patterns)")
            print(f"    Missing: {[p for p in expect_patterns if p not in matched]}")

        return success, output, matched

    except Exception as e:
        proc.kill()
        proc.wait()
        print(f"  ERROR: {e}")
        return False, "", []


TESTS = {}


def register_test(
    name,
    patterns,
    timeout=DEFAULT_TIMEOUT,
    resolution=None,
    extra_args="",
    build_first=True,
):
    TESTS[name] = {
        "patterns": patterns,
        "timeout": timeout,
        "resolution": resolution,
        "extra_args": extra_args,
        "build_first": build_first,
    }


register_test(
    "boot",
    [
        r"\[PROC\] Process manager initialized",
        r"\[FB\] \d+x\d+ \d+bpp",
        r"\[SYSCALL\] syscall/sysret initialized",
        r"\[TIMER\] PIT at 100 Hz, scheduling enabled",
        r"Backbuffers: 2 buffers",
    ],
    timeout=20,
)

register_test(
    "modules",
    [
        r"'DEMO\.SYS' loaded",
        r"'KEYB\.SYS' loaded",
        r"'MOUSE\.SYS' loaded",
        r"'TIMER\.SYS' loaded",
        r"Loaded 4 module",
    ],
    timeout=20,
    build_first=False,
)

register_test(
    "4k",
    [
        r"\[FB\] \d+x\d+ \d+bpp",
        r"\[PROC\] Process manager initialized",
        r"\[SYSCALL\] syscall/sysret initialized",
        r"Loaded 4 module",
    ],
    timeout=30,
    resolution="4k",
)

register_test(
    "cmd",
    [
        r"Loaded 4 module",
    ],
    timeout=20,
    build_first=False,
)

register_test(
    "full-boot",
    [
        r"\[PROC\] Process manager initialized",
        r"\[FB\] \d+x\d+ \d+bpp",
        r"Backbuffers: 2 buffers",
        r"Loaded 4 module",
    ],
    timeout=25,
    build_first=False,
)


def run_all_tests():
    results = {}
    for name in sorted(TESTS.keys()):
        test = TESTS[name]
        print(f"\n{'=' * 60}")
        print(f"Test: {name}")
        print(f"{'=' * 60}")

        if test["build_first"]:
            if not build():
                results[name] = False
                continue

        success, output, matched = run_qemu(
            test["patterns"],
            timeout=test["timeout"],
            resolution=test["resolution"],
            extra_args=test["extra_args"],
        )
        results[name] = success

        if not success:
            logfile = f"/tmp/blueos_test_{name}.log"
            with open(logfile, "w") as f:
                f.write(output[-5000:])
            print(f"    Last output saved to {logfile}")

    print(f"\n{'=' * 60}")
    print(f"RESULTS")
    print(f"{'=' * 60}")
    passed = sum(1 for v in results.values() if v)
    total = len(results)
    for name, ok in sorted(results.items()):
        status = "PASS" if ok else "FAIL"
        print(f"  {status}: {name}")
    print(f"\n{passed}/{total} tests passed")

    return all(results.values())


def main():
    parser = argparse.ArgumentParser(description="BlueOS QEMU Test Framework")
    parser.add_argument("test", nargs="?", help="Specific test to run (omit for all)")
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT,
        help=f"Test timeout in seconds (default: {DEFAULT_TIMEOUT})",
    )
    parser.add_argument("--list", action="store_true", help="List available tests")

    args = parser.parse_args()

    if args.list:
        print("Available tests:")
        for name in sorted(TESTS.keys()):
            test = TESTS[name]
            print(
                f"  {name}: {len(test['patterns'])} patterns, timeout={test['timeout']}s"
                f"{' [4K]' if test['resolution'] == '4k' else ''}"
            )
        return 0

    if args.test:
        if args.test not in TESTS:
            print(f"Unknown test: {args.test}")
            print(f"Available: {', '.join(sorted(TESTS.keys()))}")
            return 1
        test = TESTS[args.test]
        if test["build_first"]:
            if not build():
                return 1
        success, output, _ = run_qemu(
            test["patterns"],
            timeout=args.timeout,
            resolution=test["resolution"],
            extra_args=test["extra_args"],
        )
        return 0 if success else 1
    else:
        success = run_all_tests()
        return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
