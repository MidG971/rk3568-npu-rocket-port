#!/usr/bin/env python3
"""
Revert rocket_job.c to the Apr 15 2026 working baseline.
Removes ALL diagnostic patches added this session:
  - udelay(100) before HW_SUBMIT print
  - CNA_S + CNA_OPEN in HW_SUBMIT print
  - POST500US block (udelay(500) + reads)
  - CNA_S + CNA_OPEN in TIMEOUT print
  - INTERRUPT_MASK CORE bits (already done)
  - INTERRUPT_CLEAR CORE bits (already done)
  - IRQ handler CORE bits (already done)
  - IOMMU ioremap in timeout (already done)
  - GLOBAL ioremap (already done)
This leaves only the original Apr 15 submit logic and timeout print.
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

# ---- 1. Remove udelay(100) + extended HW_SUBMIT print, revert to simple version ----
# The current state after our patches has:
#   udelay(100);    ← added by patch_diag.py
#   dev_err("HW_SUBMIT: ... CNA_S CNA_OPEN ...")  ← extended by patch_diag.py
#   udelay(500);    ← POST500US from our current patch
#   dev_err("POST500US: ...")
# We want:
#   dev_err("HW_SUBMIT: ... AMOUNTS TASKST")   ← Apr 15 baseline (no udelays, no CNA)

old_submit_end = (
    '\tudelay(100);\n'
    '\n'
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE));\n'
    '\n'
    '\t/* ---- READ-ONLY DIAG: check if broadcast fired (no writes = no races) ---- */\n'
    '\t/* Wait 500us: PC processes ~130 config entries, broadcast would have fired by now. */\n'
    '\tudelay(500);\n'
    '\tdev_err(core->dev,\n'
    '\t\t"POST500US: CNA_S=0x%08x CNA_OPEN=0x%08x CORE_S=0x%08x TASKST=0x%08x PC_RS=0x%08x",\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_core_readl(core, S_STATUS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS));\n'
    '\t/* CNA_S=0 + CNA_OPEN=0 = broadcast never wrote sub-unit op_en */\n'
    '}'
)
new_submit_end = (
    '\tdev_err(core->dev,\n'
    '\t\t"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x",\n'
    '\t\ttask->regcmd, task->regcmd_count, scale,\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS));\n'
    '}'
)

if old_submit_end in content:
    content = content.replace(old_submit_end, new_submit_end, 1)
    print("HW_SUBMIT section reverted to Apr 15 baseline OK")
    changes += 1
else:
    print("WARN: HW_SUBMIT extended pattern not found, searching partial...")
    # Show what the end of hw_submit looks like
    idx = content.find("HW_SUBMIT:")
    if idx >= 0:
        print("HW_SUBMIT context:", repr(content[idx:idx+500]))

# ---- 2. Revert TIMEOUT print: remove CNA_S and CNA_OPEN ----
old_timeout = (
    '\tdev_err(core->dev, "PC RAW_STATUS=0x%08x OP_EN=0x%08x TASK_STATUS=0x%08x AMOUNTS=0x%08x CNA_S=0x%08x CNA_OPEN=0x%08x",\n'
    '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS),\n'
    '\t\trocket_pc_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS),\n'
    '\t\trocket_cna_readl(core, S_STATUS),\n'
    '\t\trocket_cna_readl(core, OPERATION_ENABLE));'
)
new_timeout = (
    '\tdev_err(core->dev, "PC RAW_STATUS=0x%08x OP_EN=0x%08x TASK_STATUS=0x%08x AMOUNTS=0x%08x",\n'
    '\t\trocket_pc_readl(core, INTERRUPT_RAW_STATUS),\n'
    '\t\trocket_pc_readl(core, OPERATION_ENABLE),\n'
    '\t\trocket_pc_readl(core, TASK_STATUS),\n'
    '\t\trocket_pc_readl(core, REGISTER_AMOUNTS));'
)
if old_timeout in content:
    content = content.replace(old_timeout, new_timeout, 1)
    print("TIMEOUT print reverted to Apr 15 baseline OK")
    changes += 1
else:
    print("WARN: TIMEOUT CNA pattern not found")
    idx = content.find("RAW_STATUS")
    if idx >= 0:
        print("TIMEOUT context:", repr(content[idx:idx+300]))

if changes < 2:
    print(f"Only {changes}/2 changes made — may have partial state")
    # Don't exit, just warn
else:
    print("All changes applied successfully")

# Write back
import tempfile
with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
    f.write(content)
    tmpfile = f.name

subprocess.run(["scp", "-o", "ConnectTimeout=10", tmpfile, f"radxa@{IP}:{FILEPATH}"], check=True)
os.unlink(tmpfile)

# Verify
content2 = ssh(f"cat {FILEPATH}")
print("\n--- Verification ---")
print("CNA reads in HW_SUBMIT:", "CNA_S=0x%08x" in content2 and "HW_SUBMIT" in content2[:content2.find("CNA_S=0x%08x")+20])
print("POST500US present:", "POST500US" in content2)
print("udelay(100) present:", "udelay(100)" in content2)
print("udelay(500) present:", "udelay(500)" in content2)
print("CNA reads in TIMEOUT:", "CNA_OPEN=0x%08x" in content2)
print("INTERRUPT_MASK CORE:", "PC_INTERRUPT_MASK_CORE_0" in content2)
print("IRQ CORE check:", "PC_INTERRUPT_RAW_STATUS_CORE_0" in content2)
print("Any ioremap:", "ioremap" in content2)
print("REGCMD tail dump (last 8):", "regcmd_count - 4" in content2 or "regcmd_count-4" in content2)
