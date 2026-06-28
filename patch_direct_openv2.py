#!/usr/bin/env python3
"""
Patch rocket_job.c to test the 'broadcast broken' hypothesis:
- After PC kick, wait 500us for PC to process all config regcmd entries
- Read CNA/CORE S_STATUS (should be 0 if broadcast never fired)
- Directly write CNA_OP_EN and CORE_OP_EN from kernel (bypass broadcast)
- Read S_STATUS again: if non-zero, broadcast was broken and kernel write fixes it
- Also add GLOBAL_OP_EN readback at timeout via ioremap
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

# ---- 1. After HW_SUBMIT print, add direct op_en writes ----
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
    '\t/* ---- TEST: bypass regcmd broadcast, write op_en directly ---- */\n'
    '\t/* Extra delay: PC needs ~10us to process 130 config entries at AXI speeds. */\n'
    '\t/* We wait 500us to be safe: all config loaded, GLOBAL_OP_EN broadcast would have fired. */\n'
    '\tudelay(500);\n'
    '\tdev_err(core->dev, "PRE_DIRECT_KICK: CNA_S=0x%08x CNA_OPEN=0x%08x CORE_S=0x%08x TASKST=0x%08x",\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_core_readl(core, S_STATUS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS));\n'
    '\t/* If broadcast (target=0x0081) worked, CNA_S would already be non-zero here. */\n'
    '\t/* If CNA_S=0 here, broadcast is broken on RK3568. Write op_en directly: */\n'
    '\trocket_cna_writel(core, OPERATION_ENABLE, CNA_OPERATION_ENABLE_OP_EN(1));\n'
    '\trocket_core_writel(core, OPERATION_ENABLE, CORE_OPERATION_ENABLE_OP_EN(1));\n'
    '\tudelay(100);\n'
    '\tdev_err(core->dev, "POST_DIRECT_KICK: CNA_S=0x%08x CNA_OPEN=0x%08x CORE_S=0x%08x",\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_core_readl(core, S_STATUS));\n'
    '}'
)

if old_hw_submit_end in content:
    content = content.replace(old_hw_submit_end, new_hw_submit_end, 1)
    print("HW_SUBMIT direct op_en test added OK")
else:
    print("ERROR: HW_SUBMIT end marker not found")
    print("Searching for partial match...")
    # Try to find partial
    for line in old_hw_submit_end.split('\n'):
        if line and line not in content:
            print(f"  Missing: {repr(line)}")
    sys.exit(1)

# ---- 2. At timeout, add GLOBAL_OP_EN readback via ioremap ----
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
    '\t/* GLOBAL_OP_EN: did the regcmd broadcast (target=0x0081) write to 0xfde4f008? */\n'
    '\t{\n'
    '\t\tvoid __iomem *glb = ioremap(0xfde4f000, 0x10);\n'
    '\t\tif (glb) {\n'
    '\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t"TIMEOUT GLOBAL: OP_EN=0x%08x (expect 0x1d if broadcast worked, 0x00 if not)",\n'
    '\t\t\t\treadl(glb + 0x08));\n'
    '\t\t\tiounmap(glb);\n'
    '\t\t}\n'
    '\t}'
)

if old_timeout_iommu in content:
    content = content.replace(old_timeout_iommu, new_timeout_iommu, 1)
    print("TIMEOUT GLOBAL_OP_EN readback added OK")
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
print("PRE_DIRECT_KICK present:", "PRE_DIRECT_KICK" in content2)
print("POST_DIRECT_KICK present:", "POST_DIRECT_KICK" in content2)
print("TIMEOUT GLOBAL present:", "TIMEOUT GLOBAL" in content2)
print("rocket_cna_writel OPERATION_ENABLE present:", "rocket_cna_writel(core, OPERATION_ENABLE" in content2)
