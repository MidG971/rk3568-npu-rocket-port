#!/bin/bash
# Apply pc_data_amount_scale fix to Rocket driver on board.
#
# Root cause: The RK3568 PC counts individual 64-bit regcmd words per
# REGISTER_AMOUNTS unit (scale=1), but RK3588 counts 128-bit pairs
# (scale=2).  The Rocket driver hardcoded the RK3588 formula, so on
# RK3568 the PC read only half the regcmd entries and never reached the
# final broadcast OP_EN that starts CNA/CORE/DPU.  Result: every job
# timed out with RAW_STATUS=0 (no engine completion events).
#
# Fix: add pc_data_amount_scale to rocket_soc_data and use it in
# rocket_job_hw_submit when computing PC_REGISTER_AMOUNTS.

set -e

BOARD_IP="${1:-$(cat ~/.rock3b_ip 2>/dev/null || echo 192.168.1.163)}"
ROCKET_DIR="~/npu-port/linux-mainline/drivers/accel/rocket"

echo "=== Applying pc_data_amount_scale fix to $BOARD_IP ==="

# ---- patch rocket_core.h ----
echo "  patching rocket_core.h ..."
ssh "radxa@${BOARD_IP}" "python3 - <<'PY'
import re, sys

path = '$ROCKET_DIR/rocket_core.h'.replace('~', '/home/radxa')

with open(path) as f:
    src = f.read()

old = '''/**
 * struct rocket_soc_data - per-SoC configuration data
 * @dma_bits: Physical address width reachable by the NPU\'s AXI bus.
 *            RK3568: 32 (32-bit AXI bus — GEM pages and page-table walker
 *            must reside within the first 4 GB of RAM), RK3588: 40.
 * @noc_init: optional callback to de-idle the NPU NOC bus at probe time.
 *            Required on RK3568 where the NOC must be explicitly un-idled
 *            before the NPU can be accessed.
 */
struct rocket_soc_data {
\tunsigned int dma_bits;
\tint (*noc_init)(struct rocket_core *core);
};'''

new = '''/**
 * struct rocket_soc_data - per-SoC configuration data
 * @dma_bits: Physical address width reachable by the NPU\'s AXI bus.
 *            RK3568: 32 (32-bit AXI bus — GEM pages and page-table walker
 *            must reside within the first 4 GB of RAM), RK3588: 40.
 * @noc_init: optional callback to de-idle the NPU NOC bus at probe time.
 *            Required on RK3568 where the NOC must be explicitly un-idled
 *            before the NPU can be accessed.
 * @pc_data_amount_scale: divisor used to convert regcmd entry count to the
 *            PC_REGISTER_AMOUNTS hardware field value.  RK3568 counts one
 *            64-bit regcmd word per unit (scale = 1); RK3588 counts two
 *            64-bit words (one 128-bit pair) per unit (scale = 2).  Using
 *            the wrong value causes the PC to stop reading the regcmd early
 *            and never reach the final broadcast OP_EN that starts the
 *            CNA/CORE/DPU engines.
 */
struct rocket_soc_data {
\tunsigned int dma_bits;
\tint (*noc_init)(struct rocket_core *core);
\tunsigned int pc_data_amount_scale;
};'''

if old not in src:
    # Already patched or different state
    if 'pc_data_amount_scale' in src:
        print('rocket_core.h already has pc_data_amount_scale — skip')
        sys.exit(0)
    print('ERROR: cannot find expected text in rocket_core.h', file=sys.stderr)
    sys.exit(1)

src = src.replace(old, new, 1)
with open(path, 'w') as f:
    f.write(src)
print('rocket_core.h patched OK')
PY
"

# ---- patch rocket_drv.c ----
echo "  patching rocket_drv.c ..."
ssh "radxa@${BOARD_IP}" "python3 - <<'PY'
import sys

path = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_drv.c'

with open(path) as f:
    src = f.read()

if 'pc_data_amount_scale' in src:
    print('rocket_drv.c already has pc_data_amount_scale — skip')
    sys.exit(0)

# Add scale=1 to rk3568_soc_data
old_3568 = '''.dma_bits = 32,
\t.noc_init = rk3568_noc_init,
};'''

new_3568 = '''.dma_bits = 32,
\t.noc_init = rk3568_noc_init,
\t.pc_data_amount_scale = 1,
};'''

if old_3568 not in src:
    print('ERROR: cannot find rk3568_soc_data in rocket_drv.c', file=sys.stderr)
    sys.exit(1)
src = src.replace(old_3568, new_3568, 1)

# Add scale=2 to rk3588_soc_data
# The rk3588 struct has only .dma_bits = 40
old_3588 = '''.dma_bits = 40,
};'''
new_3588 = '''.dma_bits = 40,
\t.pc_data_amount_scale = 2,
};'''

if old_3588 not in src:
    print('ERROR: cannot find rk3588_soc_data in rocket_drv.c', file=sys.stderr)
    sys.exit(1)
src = src.replace(old_3588, new_3588, 1)

with open(path, 'w') as f:
    f.write(src)
print('rocket_drv.c patched OK')
PY
"

# ---- patch rocket_job.c ----
echo "  patching rocket_job.c ..."
ssh "radxa@${BOARD_IP}" "python3 - <<'PY'
import sys

path = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'

with open(path) as f:
    src = f.read()

if 'pc_data_amount_scale' in src:
    print('rocket_job.c already has pc_data_amount_scale fix — skip')
    sys.exit(0)

old = '''\tstruct rocket_task *task;
\tunsigned int extra_bit;

\t/* Don\'t queue the job if a reset is in progress */'''

new = '''\tstruct rocket_task *task;
\tunsigned int extra_bit;
\tunsigned int scale = core->soc_data->pc_data_amount_scale;

\t/* Don\'t queue the job if a reset is in progress */'''

if old not in src:
    print('ERROR: cannot find function header in rocket_job.c', file=sys.stderr)
    sys.exit(1)
src = src.replace(old, new, 1)

old_amt = '''\trocket_pc_writel(core, REGISTER_AMOUNTS,
\t\t\t PC_REGISTER_AMOUNTS_PC_DATA_AMOUNT((task->regcmd_count + 1) / 2 - 1));'''
new_amt = '''\trocket_pc_writel(core, REGISTER_AMOUNTS,
\t\t\t PC_REGISTER_AMOUNTS_PC_DATA_AMOUNT(
\t\t\t\t (task->regcmd_count + scale - 1) / scale - 1));'''

if old_amt not in src:
    print('ERROR: cannot find REGISTER_AMOUNTS write in rocket_job.c', file=sys.stderr)
    sys.exit(1)
src = src.replace(old_amt, new_amt, 1)

with open(path, 'w') as f:
    f.write(src)
print('rocket_job.c patched OK')
PY
"

echo ""
echo "=== Patches applied. Now rebuild rocket.ko on board ==="
echo "  ssh radxa@${BOARD_IP}"
echo "  cd ~/npu-port/linux-mainline"
echo "  make ARCH=arm64 CROSS_COMPILE= KBUILD_MODPOST_WARN=1 -j4 drivers/accel/rocket/"
echo "  sudo cp drivers/accel/rocket/rocket.ko /usr/lib/modules/6.19.0-rc5/kernel/drivers/accel/rocket/"
echo "  sudo modprobe -r rocket && sudo modprobe rocket"
echo "  ./run-teflon-inference.sh"
