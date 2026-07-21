#!/usr/bin/env python3
"""Diagnostic patch for rocket_job.c on the board:
1. HW_SUBMIT: add 100us delay, then read CNA S_STATUS + CNA OP_EN after firing
2. INTERRUPT_MASK: also enable CORE_0 (bit 6) in case RK3568 completes at CORE not DPU
3. IRQ handler: also check CORE_0 for RK3568 completion
4. TIMEOUT: add CNA OP_EN readback
"""
import subprocess, sys

IP = "10.160.1.44"

def ssh(cmd):
    r = subprocess.run(["ssh", "-o", "ConnectTimeout=10", f"radxa@{IP}", cmd],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"ERROR: {r.stderr}")
        sys.exit(1)
    return r.stdout

filepath = "~/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
content = ssh(f"cat {filepath}")

# ---- 1. Update HW_SUBMIT print: add delay + CNA status readback ----
old_submit = (
    '\trocket_pc_writel(core, OPERATION_ENABLE, PC_OPERATION_ENABLE_OP_EN(1));\n'
    '\t/* BSP uses pulse: assert then de-assert to trigger PC DMA */\n'
    '\trocket_pc_writel(core, OPERATION_ENABLE, 0);\n'
    '\n'
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS));\n'
    '}'
)

new_submit = (
    '\trocket_pc_writel(core, OPERATION_ENABLE, PC_OPERATION_ENABLE_OP_EN(1));\n'
    '\t/* BSP uses pulse: assert then de-assert to trigger PC DMA */\n'
    '\trocket_pc_writel(core, OPERATION_ENABLE, 0);\n'
    '\n'
    '\t/* 100us delay: let CNA start before sampling S_STATUS */\n'
    '\tudelay(100);\n'
    '\n'
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE));\n'
    '}'
)

if old_submit in content:
    content = content.replace(old_submit, new_submit, 1)
    print("HW_SUBMIT print updated OK")
else:
    print("ERROR: HW_SUBMIT marker not found")

# ---- 2. Also enable CORE_0 interrupt in INTERRUPT_MASK ----
old_imask = '\trocket_pc_writel(core, INTERRUPT_MASK, PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1);'
new_imask = ('\t/* Enable DPU_0/DPU_1 (RK3588 completion) and CORE_0/CORE_1 (RK3568: no DPU) */\n'
             '\trocket_pc_writel(core, INTERRUPT_MASK,\n'
             '\t\t\t PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1 |\n'
             '\t\t\t PC_INTERRUPT_MASK_CORE_0 | PC_INTERRUPT_MASK_CORE_1);')

if old_imask in content:
    content = content.replace(old_imask, new_imask, 1)
    print("INTERRUPT_MASK updated OK")
else:
    print("ERROR: INTERRUPT_MASK marker not found")

# ---- 3. Update INTERRUPT_CLEAR to also clear CORE bits ----
old_iclr = '\trocket_pc_writel(core, INTERRUPT_CLEAR, PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1);'
new_iclr = ('\trocket_pc_writel(core, INTERRUPT_CLEAR,\n'
            '\t\t\t PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1 |\n'
            '\t\t\t PC_INTERRUPT_CLEAR_CORE_0 | PC_INTERRUPT_CLEAR_CORE_1);')

if old_iclr in content:
    content = content.replace(old_iclr, new_iclr, 1)
    print("INTERRUPT_CLEAR updated OK")
else:
    print("ERROR: INTERRUPT_CLEAR marker not found")

# ---- 4. Update IRQ handler to also check CORE_0/CORE_1 ----
old_irq = ('	if (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||\n'
           '	      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1))')
new_irq = ('	if (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||\n'
           '	      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1 ||\n'
           '	      raw_status & PC_INTERRUPT_RAW_STATUS_CORE_0 ||  /* RK3568: no DPU */\n'
           '	      raw_status & PC_INTERRUPT_RAW_STATUS_CORE_1))')

if old_irq in content:
    content = content.replace(old_irq, new_irq, 1)
    print("IRQ handler updated OK")
else:
    print("ERROR: IRQ handler marker not found")

# ---- 5. Update TIMEOUT print: add CNA OP_EN ----
old_timeout = ('	dev_err(core->dev, "PC RAW_STATUS=0x%08x OP_EN=0x%08x TASK_STATUS=0x%08x AMOUNTS=0x%08x",\n'
               '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS),\n'
               '\t\trocket_pc_readl(core, OPERATION_ENABLE),\n'
               '\t\trocket_pc_readl(core, TASK_STATUS),\n'
               '\t\trocket_pc_readl(core, REGISTER_AMOUNTS));')
new_timeout = ('	dev_err(core->dev, "PC RAW_STATUS=0x%08x OP_EN=0x%08x TASK_STATUS=0x%08x AMOUNTS=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
               '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS),\n'
               '\t\trocket_pc_readl(core, OPERATION_ENABLE),\n'
               '\t\trocket_pc_readl(core, TASK_STATUS),\n'
               '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
               '\t\trocket_cna_readl(core, S_STATUS),\n'
               '\t\trocket_cna_readl(core, OPERATION_ENABLE));')

if old_timeout in content:
    content = content.replace(old_timeout, new_timeout, 1)
    print("TIMEOUT print updated OK")
else:
    print("ERROR: TIMEOUT print marker not found")

# Write back
ssh(f"cat > {filepath} << 'PYEOF'\n{content}\nPYEOF")
# Verify
content2 = ssh(f"cat {filepath}")
print("udelay(100):", "udelay(100)" in content2)
print("CNA_OPEN in submit:", "CNA_OPEN" in content2)
print("CORE_0 in IMASK:", "PC_INTERRUPT_MASK_CORE_0" in content2)
print("CORE_0 in IRQ:", "PC_INTERRUPT_RAW_STATUS_CORE_0" in content2)
print("CNA_OPEN in timeout:", "CNA_OPEN" in content2 and "AMOUNTS" in content2)
