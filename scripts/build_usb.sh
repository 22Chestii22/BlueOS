#!/bin/bash
# BlueOS - USB Boot Image Builder
# Creates a single bootable USB image with GRUB + kernel + FAT32 filesystem

set -e

IMAGE="blueos_usb.img"
SIZE_MB=256
MOUNT_DIR="/tmp/blueos_usb_mount"

echo "=== BlueOS USB Boot Image Builder ==="

# Cleanup
rm -f $IMAGE

# Create image
dd if=/dev/zero of=$IMAGE bs=1M count=$SIZE_MB 2>/dev/null

# Partition: GPT with BIOS boot partition + FAT32 root
echo "Creating partition table..."
sgdisk -o $IMAGE 2>/dev/null
sgdisk -n 1:2048:+1M -t 1:ef02 -c 1:"BIOS Boot" $IMAGE 2>/dev/null
sgdisk -n 2:0:0 -t 2:0700 -c 2:"BlueOS" $IMAGE 2>/dev/null

# Setup loop device
LOOP_DEV=$(sudo losetup -fP --show $IMAGE 2>/dev/null || echo "")
if [ -z "$LOOP_DEV" ]; then
    echo "Error: Could not set up loop device"
    exit 1
fi

echo "Using loop device: $LOOP_DEV"
PART_BOOT="${LOOP_DEV}p1"
PART_DATA="${LOOP_DEV}p2"

# Wait for partitions
sleep 1

# Install GRUB to BIOS boot partition
echo "Installing GRUB..."
sudo grub-install --target=i386-pc --boot-directory=$MOUNT_DIR/boot \
    --modules="part_msdos part_gpt fat ext2 biosdisk" \
    --force $LOOP_DEV 2>/dev/null || true

# Format data partition as FAT32
echo "Formatting data partition..."
sudo mkfs.fat -F32 -n "BLUEOS" $PART_DATA 2>/dev/null

# Mount and populate
echo "Mounting..."
sudo mkdir -p $MOUNT_DIR
sudo mount $PART_DATA $MOUNT_DIR

# Copy kernel
sudo mkdir -p $MOUNT_DIR/boot/grub
sudo cp kernel.elf $MOUNT_DIR/boot/
cat > /tmp/grub.cfg << 'GRUBEOF'
set default=0
set timeout=5

menuentry "BlueOS" {
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "BlueOS (serial console)" {
    multiboot2 /boot/kernel.elf
    boot
}
GRUBEOF
sudo cp /tmp/grub.cfg $MOUNT_DIR/boot/grub/

# Copy disk image contents
if [ -f "disk.img" ]; then
    echo "Copying files from disk image..."
    DISK_MOUNT="/tmp/blueos_disk_mount"
    sudo mkdir -p $DISK_MOUNT
    sudo mount -o loop,offset=$((2048 * 512)) disk.img $DISK_MOUNT 2>/dev/null || \
        sudo mount -o loop disk.img $DISK_MOUNT 2>/dev/null || true

    if [ -d "$DISK_MOUNT" ]; then
        sudo cp -r $DISK_MOUNT/* $MOUNT_DIR/ 2>/dev/null || true
        sudo umount $DISK_MOUNT 2>/dev/null || true
    fi
    sudo rmdir $DISK_MOUNT 2>/dev/null || true
fi

echo "Syncing..."
sync

echo "Unmounting..."
sudo umount $MOUNT_DIR 2>/dev/null || true
sudo rmdir $MOUNT_DIR 2>/dev/null || true
sudo losetup -d $LOOP_DEV 2>/dev/null || true

echo ""
echo "=== USB image created: $IMAGE ($SIZE_MB MB) ==="
echo "Write to USB drive: dd if=$IMAGE of=/dev/sdX bs=1M status=progress"
echo ""
