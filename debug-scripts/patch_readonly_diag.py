#!/usr/bin/env python3
"""
Read-only diagnostic patch for rocket_job.c:
- After PC kick: wait 500us, read CNA/CORE S_STATUS (should be 0 if broadcast never fired)
  NO writes — just observe to confirm broadcast hypothesis without triggering races.
- At timeout: read GLOBAL_OP_EN via ioremap to see if PC broadcast wrote there.

This is safe (no op_en writes, no IRQ races, no NPU execution triggered).
"""
import subprocess, sys

IP = "10.160.1.45"
FILEPATH = "~/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"

def ssh(cmd, check=True):
    r = subprocess.run(["ssh", "-o", "ConnectTimeout=10", f"radxa@{IP}", cmd],
                       capture_output=True, text=True)
    if check and r.returncode != 0:
        print(f"ERROR: {r.stderr}")
        sys.exit(1)
    return r.stdout

content = ssh(f"cat {FILEPATH}")

# ---- 1. After HW_SUBMIT print, add READ-ONLY status check ----
# Match the current state of the file (after patch_diag.py was applied)
old_hw_submit_end = (
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE));\n'
    '}'
)

new_hw_submit_end = (
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE));\n'
    '\n'
    '\t/* ---- READ-ONLY DIAG: check if broadcast (target=0x0081) fired ---- */\n'
    '\t/* Wait 500us: PC processes ~130 config entries, broadcast would have fired by now. */\n'
    '\tudelay(500);\n'
    '\tdev_err(core->dev,\n'
    '\t\t"POST500US: CNA_S=0x%08x CNA_OPEN=0x%08x CORE_S=0x%08x TASKST=0x%08x PC_RS=0x%08x",\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_core_readl(core, S_STATUS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS));\n'
    '\t/* If CNA_S=0 and CNA_OPEN=0: broadcast never wrote to sub-unit op_en. */\n'
    '\t/* If CNA_S!=0: broadcast worked and NPU is running (BUG A is elsewhere). */\n'
    '}'
)

if old_hw_submit_end in content:
    content = content.replace(old_hw_submit_end, new_hw_submit_end, 1)
    print("POST500US read-only diag added OK")
else:
    print("ERROR: HW_SUBMIT end marker not found")
    print("Searching for partial match...")
    for line in old_hw_submit_end.split('\n'):
        if line and line not in content:
            print(f"  Missing: {repr(line)}")
    sys.exit(1)

# ---- 2. At timeout, add GLOBAL_OP_EN readback + IOMMU STATUS readback ----
# Match the current timeout section (after patch_diag.py but before patch_direct_openv2.py)
old_timeout_iommu = (
    '\t{\n'
    '\t\tvoid __iomem *iommu_b = ioremap(0xfde4b000, 0x30);\n'
    '\t\tif (iommu_b) {\n'
    '\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t"TIMEOUT IOMMU: DTE=0x%08x ST=0x%08x FA=0x%08x RAWST=0x%08x MASK=0x%08x INTST=0x%08x",\n'
    '\t\t\t\treadl(iommu_b + 0x00),\n'
    '\t\t\t\treadl(iommu_b + 0x04),\n'
    '\t\t\t\treadl(iommu_b + 0x0c),\n'
    '\t\t\t\treadl(iommu_b + 0x14),\n'
    '\t\t\t\treadl(iommu_b + 0x1c),\n'
    '\t\t\t\treadl(iommu_b + 0x20));\n'
    '\t\t\tiounmap(iommu_b);\n'
    '\t\t}\n'
    '\t}'
)

new_timeout_iommu = (
    '\t{\n'
    '\t\tvoid __iomem *iommu_b = ioremap(0xfde4b000, 0x30);\n'
    '\t\tif (iommu_b) {\n'
    '\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t"TIMEOUT IOMMU: DTE=0x%08x ST=0x%08x FA=0x%08x RAWST=0x%08x MASK=0x%08x INTST=0x%08x",\n'
    '\t\t\t\treadl(iommu_b + 0x00),\n'
    '\t\t\t\treadl(iommu_b + 0x04),\n'
    '\t\t\t\treadl(iommu_b + 0x0c),\n'
    '\t\t\t\treadl(iommu_b + 0x14),\n'
    '\t\t\t\treadl(iommu_b + 0x1c),\n'
    '\t\t\t\treadl(iommu_b + 0x20));\n'
    '\t\t\tiounmap(iommu_b);\n'
    '\t\t}\n'
    '\t}\n'
    '\t/* GLOBAL_OP_EN at 0xfde4f008: did PC broadcast (target=0x0081) write here? */\n'
    '\t/* Expect 0x1d if broadcast worked (CNA+CORE+DPU+DPU_RDMA bits), 0x00 if not. */\n'
    '\t{\n'
    '\t\tvoid __iomem *glb = ioremap(0xfde4f000, 0x20);\n'
    '\t\tif (glb) {\n'
    '\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t"TIMEOUT GLOBAL: OP_EN=0x%08x (expect 0x1d=broadcast OK, 0x00=broadcast broken)",\n'
    '\t\t\t\treadl(glb + 0x08));\n'
    '\t\t\tiounmap(glb);\n'
    '\t\t}\n'
    '\t}'
)

if old_timeout_iommu in content:
    content = content.replace(old_timeout_iommu, new_timeout_iommu, 1)
    print("TIMEOUT GLOBAL readback added OK")
else:
    print("ERROR: TIMEOUT IOMMU marker not found")
    for line in old_timeout_iommu.split('\n'):
        if line and line not in content:
            print(f"  Missing: {repr(line)}")
    sys.exit(1)

# Write back
import tempfile, os
with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
    f.write(content)
    tmpfile = f.name

subprocess.run(["scp", "-o", "ConnectTimeout=10", tmpfile, f"radxa@{IP}:{FILEPATH}"],
               check=True)
os.unlink(tmpfile)

# Verify
content2 = ssh(f"cat {FILEPATH}")
print("POST500US present:", "POST500US" in content2)
print("TIMEOUT GLOBAL present:", "TIMEOUT GLOBAL" in content2)
print("No op_en writes:", "rocket_cna_writel" not in content2 and "rocket_core_writel" not in content2)
