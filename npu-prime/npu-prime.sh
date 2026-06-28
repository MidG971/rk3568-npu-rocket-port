#!/bin/bash
# rknpu-as-primer (RK3568) — engage the NPU executer latch via a one-shot vendor rknpu
# job, then hand off to the mainline rocket driver. The engaged HW state (S_POINTER bit16
# on all units) persists while the NPU power domain stays on (always-on PD), so rocket
# loaded afterward inherits the engaged state.
#
# Runs at boot on the dual-compatible primer DTB (NPU node = rknn-core + rk3568-rknpu),
# with rknpu+rocket blacklisted from udev auto-load so this controls the order.
#
# NOTE: this achieves ENGAGE. Output COMPLETION is a separate, still-open wall
# (TASKST=0xf000, dt_wr=0) — see engage-gate-elimination-matrix.md. This harness puts
# rocket in the engaged state for development/validation of the post-engage path.
set -u
LOG=/var/log/npu-prime.log
exec >>"$LOG" 2>&1
echo "=== npu-prime $(date '+%F %T') ==="

PRIME_DIR=/home/radxa/npu-prime
LIB=/home/radxa/rknpu2/runtime/RK3588/Linux/librknn_api/aarch64

# Clean slate: drop any auto-loaded NPU driver (no rocket job has run at boot → rmmod safe).
rmmod rocket 2>/dev/null || true
rmmod rknpu  2>/dev/null || true

# 1. Prime via vendor rknpu (binds rockchip,rk3568-rknpu on the dual-compat DTB).
if ! modprobe rknpu; then echo "FATAL: modprobe rknpu failed"; exit 1; fi
sleep 1

# 2. Run a tiny priming job → engages the executer latch (sets bit16; persists while powered).
if [ -x "$PRIME_DIR/rknn_raw_run" ] && [ -f "$PRIME_DIR/conv2d-bsp.rknn" ]; then
    LD_LIBRARY_PATH="$LIB" "$PRIME_DIR/rknn_raw_run" "$PRIME_DIR/conv2d-bsp.rknn" /tmp/npu-prime.out
    echo "prime job rc=$?"
else
    echo "WARN: priming binary/model missing — engage NOT performed"
fi

# 3. Release rknpu (rmmod after a vendor job is safe) and hand off to rocket.
rmmod rknpu || echo "WARN: rmmod rknpu failed"
sleep 1

# 4. Load rocket — inherits the engaged latch. primediff=1 makes rocket dump the NPU
#    register state at probe so this log self-verifies engage (S_POINTER bit16 = 0x1000e).
#    Do NOT rmmod+reload this rocket instance afterwards — the inherited engage is lost
#    across a rocket reload (only the rknpu->rocket handoff carries it).
if ! modprobe rocket primediff=1; then echo "FATAL: modprobe rocket failed"; exit 1; fi
sleep 1
echo "done: $(lsmod | grep -E '^rocket|^rknpu' | tr '\n' ' ')"
echo "accel node: $(ls /dev/accel/accel0 2>/dev/null || echo MISSING)"
echo "--- engage self-check (PRIMEDIFF S_POINTER; 0x1000e = bit16 = ENGAGED) ---"
dmesg | grep -E 'PRIMEDIFF off=0x(1004|2004|3004|4004|5004)' | tail -5
