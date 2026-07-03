#!/bin/bash
# Build + load + snapshot + unload the read-only NPU probe on the board.
# READ-ONLY: only reads NPU registers via a kernel module (safe); never writes,
# never touches 0xf008, runs NO NPU job. Safe on a primed/idle board.
#
# Usage (on the board, from this dir):  ./probe.sh
set -e
cd "$(dirname "$0")"

# locate a kernel build dir
KDIR="/lib/modules/$(uname -r)/build"
[ -d "$KDIR" ] || KDIR="$HOME/npu-port/linux-mainline"
[ -d "$KDIR" ] || { echo "no kernel build dir found (tried /lib/modules/.../build and ~/npu-port/linux-mainline)"; exit 1; }
echo "KDIR=$KDIR"

make KDIR="$KDIR" 2>&1 | tail -3

sudo dmesg -C
sudo insmod npu_probe.ko
sudo rmmod npu_probe
echo "===== snapshot ====="
sudo dmesg | sed -n 's/.*npu_probe: //p'
