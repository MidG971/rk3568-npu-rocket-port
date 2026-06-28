#!/bin/bash
# continue-fixes.sh - Apply remaining fixes to rknpu driver
# Run this to continue where we left off

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/rock3b-ssh.sh" 2>/dev/null || true

ROCK3B_IP=$(cat ~/.rock3b_ip 2>/dev/null)
if [ -z "$ROCK3B_IP" ]; then
    echo "Finding ROCK 3B..."
    "$SCRIPT_DIR/find-rock3b.sh"
    ROCK3B_IP=$(cat ~/.rock3b_ip)
fi

echo "=== Applying Remaining Fixes to rknpu_drv.c ==="
echo "Target: radxa@$ROCK3B_IP"
echo ""

# Fix 1: Add hrtimer.h include
echo "1. Adding #include <linux/hrtimer.h>..."
ssh radxa@$ROCK3B_IP "cd ~/npu-port/linux-mainline && \
    grep -q 'linux/hrtimer.h' drivers/rknpu/rknpu_drv.c || \
    sed -i '/#include <linux\/ktime.h>/a #include <linux/hrtimer.h>' drivers/rknpu/rknpu_drv.c"

# Fix 2: Change return 0 to return in void function
echo "2. Fixing return statement in rknpu_remove..."
ssh radxa@$ROCK3B_IP "cd ~/npu-port/linux-mainline && \
    sed -i '/^static void rknpu_remove/,/^}/s/return 0;/return;/' drivers/rknpu/rknpu_drv.c"

echo ""
echo "=== Testing Compilation ==="
ssh radxa@$ROCK3B_IP "cd ~/npu-port/linux-mainline && \
    make ARCH=arm64 CROSS_COMPILE= drivers/rknpu/rknpu_drv.o 2>&1" | head -50

echo ""
echo "=== Done ==="
echo "If successful, run: ./rock3b-ssh.sh 'cd ~/npu-port/linux-mainline && git add -A && git commit -m \"rknpu: Fix DRM API changes for kernel 6.19\"'"
