#!/usr/bin/env python3
# Add a gated reg-write trace to rocket's 3 CPU->NPU write macros, comparable to
# the BSP RKNPU_WTRACE. Logs raw REG_*_##reg constants = absolute NPU offsets
# (PC 0x0-based, CNA 0x1000-based, CORE 0x3000-based) so they diff directly vs BSP.
import sys, time, shutil

HDR = "/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_core.h"
JOB = "/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"

def patch(path, replacements, marker):
    with open(path) as f:
        src = f.read()
    if marker in src:
        print("%s ALREADY PATCHED" % path); return True
    for old, new in replacements:
        if src.count(old) != 1:
            print("ANCHOR not unique in %s (count=%d): %r" % (path, src.count(old), old[:50]))
            return False
    shutil.copy(path, path + ".bak-wtrace-" + time.strftime("%Y%m%d-%H%M%S"))
    for old, new in replacements:
        src = src.replace(old, new, 1)
    with open(path, "w") as f:
        f.write(src)
    print("%s PATCHED" % path); return True

# --- header: declare extern + wrap the 3 macros ---
hdr_repl = [
(
"#define rocket_pc_writel(core, reg, value) \\\n"
"\twritel(value, (core)->pc_iomem + (REG_PC_##reg))",
"extern int rocket_wtrace;\n"
"#define rocket_pc_writel(core, reg, value) do { \\\n"
"\tif (rocket_wtrace) pr_info(\"ROCKET_WTRACE PC off=0x%x val=0x%x\\n\", (u32)(REG_PC_##reg), (u32)(value)); \\\n"
"\twritel(value, (core)->pc_iomem + (REG_PC_##reg)); \\\n"
"} while (0)",
),
(
"#define rocket_cna_writel(core, reg, value) \\\n"
"\twritel(value, (core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS)",
"#define rocket_cna_writel(core, reg, value) do { \\\n"
"\tif (rocket_wtrace) pr_info(\"ROCKET_WTRACE CNA off=0x%x val=0x%x\\n\", (u32)(REG_CNA_##reg), (u32)(value)); \\\n"
"\twritel(value, (core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS); \\\n"
"} while (0)",
),
(
"#define rocket_core_writel(core, reg, value) \\\n"
"\twritel(value, (core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS)",
"#define rocket_core_writel(core, reg, value) do { \\\n"
"\tif (rocket_wtrace) pr_info(\"ROCKET_WTRACE CORE off=0x%x val=0x%x\\n\", (u32)(REG_CORE_##reg), (u32)(value)); \\\n"
"\twritel(value, (core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS); \\\n"
"} while (0)",
),
]

# --- job.c: define var + module_param right after group_probe's PARM_DESC ---
job_anchor = 'MODULE_PARM_DESC(group_probe, "Sample CNA group geometry/engage during execution (1=on)");'
job_repl = [(
job_anchor,
job_anchor + "\n"
"int rocket_wtrace;\n"
"module_param(rocket_wtrace, int, 0644);\n"
'MODULE_PARM_DESC(rocket_wtrace, "Trace every CPU NPU register write (PC/CNA/CORE) via pr_info (1=on)");',
)]

ok1 = patch(HDR, hdr_repl, "ROCKET_WTRACE PC off")
ok2 = patch(JOB, job_repl, "module_param(rocket_wtrace")
sys.exit(0 if (ok1 and ok2) else 1)
