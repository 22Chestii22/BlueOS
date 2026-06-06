#!/usr/bin/env python3
"""Build a .blu executable from a flat NASM binary (x86-64)."""

import struct
import sys
import os


def make_blu(bin_path, out_path, base=0x400000, entry_rva=0x1000, name="", bss_size=0):
    with open(bin_path, "rb") as f:
        code = f.read()

    code_size = len(code)
    image_size = code_size + bss_size
    if not name:
        name = os.path.splitext(os.path.basename(out_path))[0]
    name_bytes = name.encode("utf-8")[:31] + b"\x00"

    magic = b"BLU\x01"
    header = struct.pack(
        "<4sIIQQ32s", magic, entry_rva, base, image_size, bss_size, name_bytes
    )
    assert len(header) == 60

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(code)

    entry = base + entry_rva
    print(f"Wrote {out_path} ({os.path.getsize(out_path)} bytes, code={code_size}B)")
    print(f"  Entry: 0x{entry:x}  ImageBase: 0x{base:x}")
    print(f"  ImageSize: 0x{image_size:x}  BSS: 0x{bss_size:x}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(
            f"Usage: {sys.argv[0]} input.bin output.blu [base] [entry_rva] [name] [bss_size]"
        )
        sys.exit(1)
    bin_path = sys.argv[1]
    out_path = sys.argv[2]
    base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x400000
    entry_rva = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x1000
    name = sys.argv[5] if len(sys.argv) > 5 else ""
    bss_size = int(sys.argv[6], 0) if len(sys.argv) > 6 else 0
    make_blu(bin_path, out_path, base, entry_rva, name, bss_size)
