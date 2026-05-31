#!/bin/bash
# BlueOS - Disk Image Builder
# Creates a FAT32 disk image with DOS directory structure.
# Uses mtools (mformat, mmd, mcopy) when available to avoid needing root.

set -e

PROJECT_ROOT="$(dirname "$0")/.."
PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"

IMAGE="$PROJECT_ROOT/disk.img"
SIZE_MB=128
MOUNT_DIR="/tmp/blueos_mount"

echo "=== BlueOS Disk Image Builder ==="

# Create empty disk image (superfloppy — no MBR partition table)
# Kernel reads LBA 0 directly as FAT32 VBR, so no partition table allowed.
echo "Creating disk image ($SIZE_MB MB)..."
dd if=/dev/zero of="$IMAGE" bs=1M count=$SIZE_MB 2>/dev/null

# Use mtools (preferred, no sudo needed)
if command -v mformat &> /dev/null; then
    echo "Using mtools (no root required)..."

    # Format whole disk as FAT32 (superfloppy — VBR at LBA 0)
    mformat -i "$IMAGE" -F :: 2>/dev/null || \
        mformat -i "$IMAGE" -t 127 -h 64 -s 32 -F ::

    # Create DOS directory structure (8.3 uppercase)
    echo "Creating DOS directory structure..."
    mmd -i "$IMAGE" ::/SYSTEM
    mmd -i "$IMAGE" ::/PROGRAMS
    mmd -i "$IMAGE" ::/TEMP
    mmd -i "$IMAGE" ::/USERS
    mmd -i "$IMAGE" ::/USERS/DEFAULT

    # Copy executables
    echo "Copying executables..."
    if [ -f "$PROJECT_ROOT/programs/edit/edit.exe" ]; then
        mcopy -i "$IMAGE" "$PROJECT_ROOT/programs/edit/edit.exe" ::/PROGRAMS/EDIT.EXE
        echo "  EDIT.EXE"
    fi
    if [ -f "$PROJECT_ROOT/programs/test.exe" ]; then
        mcopy -i "$IMAGE" "$PROJECT_ROOT/programs/test.exe" ::/PROGRAMS/TEST.EXE
        echo "  TEST.EXE"
    fi

    # Create AUTOEXEC.BAT
    echo "Creating AUTOEXEC.BAT..."
    {
        echo "@ECHO OFF"
        echo "PATH C:\\SYSTEM;C:\\PROGRAMS"
        echo "PROMPT \$P\$G"
    } | mcopy -i "$IMAGE" - ::/AUTOEXEC.BAT

    # Create CONFIG.SYS
    echo "Creating CONFIG.SYS..."
    {
        echo "FILES=30"
        echo "BUFFERS=20"
    } | mcopy -i "$IMAGE" - ::/CONFIG.SYS

# Fallback: use loop+mount (requires sudo)
else
    echo "mtools not available, using loop+mount (requires sudo)..."
    echo

    LOOP_DEV=$(sudo losetup -f --show "$IMAGE")

    echo "Formatting FAT32..."
    sudo mkfs.fat -F32 -n "BLUEOS" "$LOOP_DEV" > /dev/null 2>&1

    echo "Mounting..."
    sudo mkdir -p "$MOUNT_DIR"
    sudo mount "$LOOP_DEV" "$MOUNT_DIR"

    echo "Creating DOS directory structure..."
    sudo mkdir -p "$MOUNT_DIR"/SYSTEM
    sudo mkdir -p "$MOUNT_DIR"/PROGRAMS
    sudo mkdir -p "$MOUNT_DIR"/TEMP
    sudo mkdir -p "$MOUNT_DIR"/USERS
    sudo mkdir -p "$MOUNT_DIR"/USERS/DEFAULT

    echo "Copying executables..."
    if [ -f "$PROJECT_ROOT/programs/edit/edit.exe" ]; then
        sudo cp "$PROJECT_ROOT/programs/edit/edit.exe" "$MOUNT_DIR"/PROGRAMS/EDIT.EXE
        echo "  EDIT.EXE"
    fi
    if [ -f "$PROJECT_ROOT/programs/test.exe" ]; then
        sudo cp "$PROJECT_ROOT/programs/test.exe" "$MOUNT_DIR"/PROGRAMS/TEST.EXE
        echo "  TEST.EXE"
    fi

    echo "Creating AUTOEXEC.BAT..."
    echo "@ECHO OFF" | sudo tee "$MOUNT_DIR"/AUTOEXEC.BAT > /dev/null
    echo "PATH C:\\SYSTEM;C:\\PROGRAMS" | sudo tee -a "$MOUNT_DIR"/AUTOEXEC.BAT > /dev/null
    echo "PROMPT \$P\$G" | sudo tee -a "$MOUNT_DIR"/AUTOEXEC.BAT > /dev/null

    echo "Creating CONFIG.SYS..."
    echo "FILES=30" | sudo tee "$MOUNT_DIR"/CONFIG.SYS > /dev/null
    echo "BUFFERS=20" | sudo tee -a "$MOUNT_DIR"/CONFIG.SYS > /dev/null

    echo "Syncing..."
    sync

    echo "Unmounting..."
    sudo umount "$MOUNT_DIR"
    sudo rmdir "$MOUNT_DIR"
    sudo losetup -d "$LOOP_DEV"
fi

echo ""
echo "=== Disk image created: $IMAGE ($SIZE_MB MB) ==="
echo "Directory structure:"
echo "  /SYSTEM/       - System files"
echo "  /PROGRAMS/     - Executable programs"
echo "  /TEMP/         - Temporary files"
echo "  /USERS/        - User profiles"
echo "  /USERS/DEFAULT - Default user profile"
