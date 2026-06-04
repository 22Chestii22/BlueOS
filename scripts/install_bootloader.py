#!/usr/bin/env python3
"""
BlueOS Bootloader Installer

Assembles stage1 and stage2, patches stage1 BPB from mformat,
and writes both to the disk image.

Usage: python3 install_bootloader.py <disk_image>
"""

import subprocess
import sys
import os

STAGE1_SRC = "bootloader/stage1.asm"
STAGE1_BIN = "bootloader/stage1.bin"
STAGE2_SRC = "bootloader/stage2.asm"
STAGE2_BIN = "bootloader/stage2.bin"

STAGE2_LBA_START = 7
STAGE2_MAX_SECTORS = 24  # 12KB


def assemble_nasm(src, out):
    """Run NASM to assemble a flat binary."""
    result = subprocess.run(
        ["nasm", "-f", "bin", "-o", out, src], capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"NASM error for {src}:")
        print(result.stderr)
        return False
    return True


def patch_bpb(stage1_bin_path, mbr_data):
    """
    Patch stage1.bin with the BPB from the mformatted VBR.

    BPB is at bytes 0x0B-0x59 in the boot sector (offsets relative to start).
    We copy these bytes from mformat's VBR into our stage1 binary,
    preserving our jump instruction and boot code.
    Returns the patched 512-byte boot sector.
    """
    with open(stage1_bin_path, "rb") as f:
        stage1 = bytearray(f.read())

    mbr = bytearray(mbr_data)

    for offset in range(0x0B, 0x5A):
        if offset < len(stage1) and offset < len(mbr):
            stage1[offset] = mbr[offset]

    stage1[510] = 0x55
    stage1[511] = 0xAA

    return bytes(stage1)


def write_to_disk(disk_img, lba_start, data, sector_size=512):
    """Write data to disk image at given LBA."""
    with open(disk_img, "r+b") as f:
        f.seek(lba_start * sector_size)
        f.write(data)
        # Pad to sector boundary if needed
        remainder = len(data) % sector_size
        if remainder:
            f.write(b"\x00" * (sector_size - remainder))


def read_from_disk(disk_img, lba_start, size, sector_size=512):
    """Read data from disk image at given LBA."""
    with open(disk_img, "rb") as f:
        f.seek(lba_start * sector_size)
        return f.read(size)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <disk_image>")
        sys.exit(1)

    disk_img = sys.argv[1]
    if not os.path.exists(disk_img):
        print(f"Error: disk image '{disk_img}' not found. Run 'make disk.img' first.")
        sys.exit(1)

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(project_root)

    print("=== BlueOS Bootloader Installer ===")

    # Step 1: Assemble stage1
    print("Assembling stage1...")
    if not assemble_nasm(STAGE1_SRC, STAGE1_BIN):
        sys.exit(1)

    # Step 2: Assemble stage2
    print("Assembling stage2...")
    if not assemble_nasm(STAGE2_SRC, STAGE2_BIN):
        sys.exit(1)

    stage2_size = os.path.getsize(STAGE2_BIN)
    stage2_sectors = (stage2_size + 511) // 512
    if stage2_sectors > STAGE2_MAX_SECTORS:
        print(
            f"Error: stage2 is {stage2_size} bytes ({stage2_sectors} sectors), "
            f"but only {STAGE2_MAX_SECTORS} sectors available!"
        )
        sys.exit(1)
    print(f"  Stage2: {stage2_size} bytes ({stage2_sectors} sectors)")

    # Step 3: Read current VBR from disk (mformat created this)
    print("Reading current VBR from disk...")
    mbr = read_from_disk(disk_img, 0, 512)
    if len(mbr) < 512:
        print("Error: disk image too small!")
        sys.exit(1)

    # Step 4: Patch stage1 with BPB from mformat's VBR
    print("Patching stage1 BPB...")
    stage1_data = patch_bpb(STAGE1_BIN, mbr)

    # Step 5: Write stage1 to LBA 0
    print(f"Writing stage1 to LBA 0 ({STAGE1_BIN})...")
    write_to_disk(disk_img, 0, stage1_data)

    # Step 6: Write stage2 to LBA STAGE2_LBA_START
    print(f"Writing stage2 to LBA {STAGE2_LBA_START}...")
    with open(STAGE2_BIN, "rb") as f:
        stage2_data = f.read()
    write_to_disk(disk_img, STAGE2_LBA_START, stage2_data)

    # Verify
    print("Verifying bootloader...")
    verify = read_from_disk(disk_img, 0, 512)
    if verify[510] == 0x55 and verify[511] == 0xAA:
        print("  Stage1 signature OK")
    else:
        print("  WARNING: Stage1 0x55AA signature missing!")

    if len(verify) >= 3:
        jmp = verify[0]
        if jmp == 0xEB:  # short jump
            print("  Stage1 jump OK")
        else:
            print(f"  WARNING: Stage1 first byte 0x{jmp:02X}, expected 0xEB")

    print("=== Bootloader installation complete ===")


if __name__ == "__main__":
    main()
