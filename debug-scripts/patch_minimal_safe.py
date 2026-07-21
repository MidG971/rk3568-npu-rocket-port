#!/usr/bin/env python3
"""
Minimal safe patch for rocket_job.c:
- Reverts ALL patch_diag.py changes (INTERRUPT_MASK/CLEAR/IRQ CORE bits, IOMMU ioremap, udelay)
- Keeps ONLY the read-only POST500US diagnostic (udelay(500) + register reads after HW_SUBMIT)
- No op_en writes, no GLOBAL ioremap, no INTERRUPT_MASK changes
This matches the Apr 15 working baseline + one safe read-only diagnostic.
"""
import subprocess, sys, os

IP = "192.168.1.163"
FILEPATH = "~/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"

def ssh(cmd, check=True):
    r = subprocess.run(["ssh", "-o", "ConnectTimeout=10", f"radxa@{IP}", cmd],
                       capture_output=True, text=True)
    if check and r.returncode != 0:
        print(f"SSH ERROR: {r.stderr}")
        sys.exit(1)
    return r.stdout

content = ssh(f"cat {FILEPATH}")

changes = 0

# ---- 1. Revert INTERRUPT_MASK: remove CORE_0|CORE_1 ----
old_imask = (
    '\t/* Enable DPU_0/DPU_1 (RK3588 completion) and CORE_0/CORE_1 (RK3568: no DPU) */\n'
    '\trocket_pc_writel(core, INTERRUPT_MASK,\n'
    '\t\t\t PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1 |\n'
    '\t\t\t PC_INTERRUPT_MASK_CORE_0 | PC_INTERRUPT_MASK_CORE_1);'
)
new_imask = '\trocket_pc_writel(core, INTERRUPT_MASK, PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1);'
if old_imask in content:
    content = content.replace(old_imask, new_imask, 1)
    print("INTERRUPT_MASK reverted OK")
    changes += 1
else:
    print("WARN: INTERRUPT_MASK CORE variant not found (maybe already reverted)")

# ---- 2. Revert INTERRUPT_CLEAR: remove CORE_0|CORE_1 ----
old_iclr = (
    '\trocket_pc_writel(core, INTERRUPT_CLEAR,\n'
    '\t\t\t PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1 |\n'
    '\t\t\t PC_INTERRUPT_CLEAR_CORE_0 | PC_INTERRUPT_CLEAR_CORE_1);'
)
new_iclr = '\trocket_pc_writel(core, INTERRUPT_CLEAR, PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1);'
if old_iclr in content:
    content = content.replace(old_iclr, new_iclr, 1)
    print("INTERRUPT_CLEAR reverted OK")
    changes += 1
else:
    print("WARN: INTERRUPT_CLEAR CORE variant not found (maybe already reverted)")

# ---- 3. Revert IRQ handler: remove CORE_0|CORE_1 check ----
old_irq = (
    '\tif (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||\n'
    '\t      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1 ||\n'
    '\t      raw_status & PC_INTERRUPT_RAW_STATUS_CORE_0 ||  /* RK3568: no DPU */\n'
    '\t      raw_status & PC_INTERRUPT_RAW_STATUS_CORE_1))'
)
new_irq = (
    '\tif (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||\n'
    '\t      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1))'
)
if old_irq in content:
    content = content.replace(old_irq, new_irq, 1)
    print("IRQ handler reverted OK")
    changes += 1
else:
    print("WARN: IRQ CORE variant not found (maybe already reverted)")

# ---- 4. Remove IOMMU ioremap from timeout handler ----
old_iommu_block = (
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
if old_iommu_block in content:
    content = content.replace(old_iommu_block, '', 1)
    print("IOMMU ioremap block removed OK")
    changes += 1
else:
    print("WARN: IOMMU ioremap block not found (maybe already removed)")

# ---- 5. Also remove the leftover GLOBAL comment if present ----
old_global_comment = '\t/* GLOBAL block (0xfde4f000) removed: causes AXI hang on RK3568 */'
if old_global_comment in content:
    content = content.replace(old_global_comment, '', 1)
    print("GLOBAL comment cleaned up OK")
    changes += 1

if changes == 0:
    print("ERROR: no changes made — file may be in unexpected state")
    sys.exit(1)

# Write back
import tempfile
with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
    f.write(content)
    tmpfile = f.name

subprocess.run(["scp", "-o", "ConnectTimeout=10", tmpfile, f"radxa@{IP}:{FILEPATH}"], check=True)
os.unlink(tmpfile)

# Verify key properties
content2 = ssh(f"cat {FILEPATH}")
print("\n--- Verification ---")
print("POST500US present:", "POST500US" in content2)
print("INTERRUPT_MASK CORE bits:", "PC_INTERRUPT_MASK_CORE_0" in content2)  # should be False
print("IRQ CORE bits:", "PC_INTERRUPT_RAW_STATUS_CORE_0" in content2)        # should be False
print("IOMMU ioremap present:", "ioremap(0xfde4b000" in content2)             # should be False
print("GLOBAL ioremap present:", "ioremap(0xfde4f000" in content2)            # should be False
print("op_en writes present:", "rocket_cna_writel" in content2 and "OPERATION_ENABLE" in content2)  # False except setup
