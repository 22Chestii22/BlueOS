#!/usr/bin/env python3
"""Build a minimal PE32+ executable from a flat binary (x86-64)."""

import struct
import sys
import os


def make_pe(bin_path, out_path, vsize=None):
    with open(bin_path, "rb") as f:
        code = f.read()
    code_size = len(code)
    sec_va = 0x1000
    image_base = 0x400000

    file_align = 0x200
    sect_align = 0x1000
    if vsize is None:
        vsize = (code_size + sect_align - 1) & ~(sect_align - 1)
    rsize = (code_size + file_align - 1) & ~(file_align - 1)
    hdr_size = 0x200
    image_size = sec_va + vsize
    image_size = (image_size + sect_align - 1) & ~(sect_align - 1)

    # MS-DOS header
    dos = bytearray(64)
    struct.pack_into("<2s58xI", dos, 0, b"MZ", 0x40)

    # PE signature
    pe_sig = struct.pack("<4s", b"PE\0\0")

    # COFF header (20 bytes)
    coff = struct.pack(
        "<HHIIIHH",
        0x8664,  # Machine: AMD64
        1,  # NumberOfSections
        0,  # TimeDateStamp
        0,  # PointerToSymbolTable
        0,  # NumberOfSymbols
        0x00F0,  # SizeOfOptionalHeader
        0x0022,  # Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    )

    # Optional header PE32+ (240 bytes = 0xF0)
    opt = bytearray(0xF0)
    struct.pack_into("<H", opt, 0, 0x020B)  # Magic PE32+
    # MajorLinkerVersion, MinorLinkerVersion - skip (0)
    struct.pack_into("<I", opt, 4, rsize)  # SizeOfCode
    struct.pack_into("<I", opt, 8, 0)  # SizeOfInitializedData
    struct.pack_into("<I", opt, 12, 0)  # SizeOfUninitializedData
    struct.pack_into("<I", opt, 16, sec_va)  # AddressOfEntryPoint
    struct.pack_into("<I", opt, 20, sec_va)  # BaseOfCode
    struct.pack_into("<Q", opt, 24, image_base)  # ImageBase
    struct.pack_into("<I", opt, 32, sect_align)  # SectionAlignment
    struct.pack_into("<I", opt, 36, file_align)  # FileAlignment
    struct.pack_into("<HHH", opt, 40, 4, 0, 0)  # OS major/minor, Image major/minor
    struct.pack_into("<H", opt, 48, 4)  # SubsystemVersion major
    struct.pack_into("<II", opt, 56, image_size, hdr_size)
    struct.pack_into("<H", opt, 68, 3)  # Subsystem: CONSOLE
    struct.pack_into("<H", opt, 70, 0)  # DllCharacteristics
    struct.pack_into("<QQQQ", opt, 72, 0x100000, 0x1000, 0x100000, 0x1000)
    # SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit
    struct.pack_into("<II", opt, 108, 0, 16)  # LoaderFlags, NumberOfRvaAndSizes

    # Section header .text (40 bytes)
    sname = b".text\0\0\0"
    sec = struct.pack(
        "<8sIIIIIIHHI",
        sname,
        vsize,  # VirtualSize
        sec_va,  # VirtualAddress
        rsize,  # SizeOfRawData
        hdr_size,  # PointerToRawData
        0,
        0,
        0,
        0,  # relocs/linenumbers
        0x60000020,  # CODE | EXECUTE | READ
    )

    # Assemble the PE
    pe = bytearray(hdr_size)
    # DOS header
    pe[0:64] = dos
    # PE signature + COFF header
    pe[0x40 : 0x40 + 4] = pe_sig
    pe[0x44 : 0x44 + 20] = coff
    # Optional header
    pe[0x58 : 0x58 + 0xF0] = opt
    # Section header
    pe[0x148 : 0x148 + 40] = sec
    # Code
    pe[hdr_size : hdr_size + code_size] = code

    with open(out_path, "wb") as f:
        f.write(pe)

    entry = image_base + sec_va
    print(f"Wrote {out_path} ({len(pe)} bytes, code={code_size}B)")
    print(f"  Entry: 0x{entry:x}  ImageBase: 0x{image_base:x}")
    print(
        f"  VirtualSize: 0x{vsize:x}  RawSize: 0x{rsize:x}  ImageSize: 0x{image_size:x}"
    )


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.bin> <output.exe> [vsize_hex]")
        sys.exit(1)
    vsize = None
    if len(sys.argv) > 3:
        vsize = int(sys.argv[3], 16)
    make_pe(sys.argv[1], sys.argv[2], vsize)
