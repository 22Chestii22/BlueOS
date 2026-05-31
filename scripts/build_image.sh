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
PART_OFFSET=1048576  # 2048 sectors * 512 bytes

echo "=== BlueOS Disk Image Builder ==="

# Create empty disk image
echo "Creating disk image ($SIZE_MB MB)..."
dd if=/dev/zero of="$IMAGE" bs=1M count=$SIZE_MB 2>/dev/null

# Create MBR partition table with FAT32 partition
echo "Creating partition table..."
fdisk "$IMAGE" > /dev/null 2>&1 <<EOF
o
n
p
1


t
c
a
1
w
EOF

# Use mtools (preferred, no sudo needed)
if command -v mformat &> /dev/null; then
    echo "Using mtools (no root required)..."
    MTOOLS_OPTS="-i ${IMAGE}@@${PART_OFFSET}"

    # Format as FAT32
    mformat $MTOOLS_OPTS -F :: 2>/dev/null || \
        mformat $MTOOLS_OPTS -t 127 -h 64 -s 32 -F ::

    # Create DOS directory structure (8.3 uppercase)
    echo "Creating DOS directory structure..."
    mmd $MTOOLS_OPTS ::/SYSTEM
    mmd $MTOOLS_OPTS ::/PROGRAMS
    mmd $MTOOLS_OPTS ::/TEMP
    mmd $MTOOLS_OPTS ::/USERS
    mmd $MTOOLS_OPTS ::/USERS/DEFAULT

    # Copy executables
    echo "Copying executables..."
    if [ -f "$PROJECT_ROOT/programs/edit/edit.exe" ]; then
        mcopy $MTOOLS_OPTS "$PROJECT_ROOT/programs/edit/edit.exe" ::/PROGRAMS/EDIT.EXE
        echo "  EDIT.EXE"
    fi
    if [ -f "$PROJECT_ROOT/programs/test.exe" ]; then
        mcopy $MTOOLS_OPTS "$PROJECT_ROOT/programs/test.exe" ::/PROGRAMS/TEST.EXE
        echo "  TEST.EXE"
    fi

    # Create AUTOEXEC.BAT
    echo "Creating AUTOEXEC.BAT..."
    {
        echo "@ECHO OFF"
        echo "PATH C:\\SYSTEM;C:\\PROGRAMS"
        echo "PROMPT \$P\$G"
    } | mcopy $MTOOLS_OPTS - ::/AUTOEXEC.BAT

    # Create CONFIG.SYS
    echo "Creating CONFIG.SYS..."
    {
        echo "FILES=30"
        echo "BUFFERS=20"
    } | mcopy $MTOOLS_OPTS - ::/CONFIG.SYS

# Fallback: use loop+mount (requires sudo)
else
    echo "mtools not available, using loop+mount (requires sudo)..."
    echo

    LOOP_DEV=$(sudo losetup -fP --show "$IMAGE" 2>/dev/null || echo "")
    if [ -z "$LOOP_DEV" ]; then
        LOOP_DEV=$(sudo losetup -f --show "$IMAGE")
        sudo partprobe "$LOOP_DEV" 2>/dev/null || true
    fi

    sudo losetup -d "$LOOP_DEV" 2>/dev/null || true
    LOOP_DEV=$(sudo losetup -f --show -o $PART_OFFSET "$IMAGE")

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
