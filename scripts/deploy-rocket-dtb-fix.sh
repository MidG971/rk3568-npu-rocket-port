#!/bin/bash
# Deploy fixed Rocket DTB to ROCK 3B for inference test
# Fix: NPU IOMMU uses "rockchip,iommu" (v1 ops, GFP_DMA32) instead of
#      "rockchip,rk3568-iommu" (v2 ops, gfp_flags=0) — prevents DTE above 4GB

set -e

BOARD_IP="${1:-$(cat ~/.rock3b_ip 2>/dev/null || echo 192.168.1.163)}"
DTB_SRC="$(dirname "$0")/rk3568-rock-3b-6.19-rocket-fixed.dtb"
DTB_DEST="/boot/rk3568-rock-3b-6.19-rocket.dtb"

echo "=== Deploying fixed Rocket DTB to $BOARD_IP ==="
echo "  Source: $DTB_SRC"
echo "  Dest:   $DTB_DEST (replaces existing l3 DTB)"
echo ""

# Copy DTB
scp "$DTB_SRC" "radxa@${BOARD_IP}:/tmp/rk3568-rock-3b-6.19-rocket.dtb"

# Install with sudo
ssh "radxa@${BOARD_IP}" "sudo cp /tmp/rk3568-rock-3b-6.19-rocket.dtb ${DTB_DEST} && echo 'DTB installed OK'"

echo ""
echo "=== Verify IOMMU compatible in installed DTB ==="
ssh "radxa@${BOARD_IP}" "dtc -I dtb -O dts ${DTB_DEST} 2>/dev/null | grep -A2 'iommu@fde4b000' | head -4"

echo ""
echo "=== Next steps ==="
echo "  1. Reboot board to l3 entry (Rocket driver)"
echo "  2. After boot: sudo modprobe rocket"
echo "  3. Check: dmesg | grep -i rocket"
echo "  4. Run inference: see run-teflon-inference.sh"
