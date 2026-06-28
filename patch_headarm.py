#!/usr/bin/env python3
# HEAD-ARM test (2026-06-19/20): replicate the BSP regcmd head-arming that mesa omits.
# BSP regcmd entries [0..2] arm CNA/CMAC/CORE S_POINTER=0xe via broadcast, AT THE HEAD,
# before config (ref: for_review/bsp-regcmd-yolov5s-20260619-153839.txt). mesa emits only
# DPU/RDMA (0x4004/0x5004) and OMITS 0x1004/0x2004/0x3004. rocket's CPU pre-arm (pp_init)
# is not the same as broadcast-at-head -> no engage.
# This PREPENDS the 3 missing arming entries to the regcmd head (broadcast), gated by `head_arm`.
# Run with: insmod ... head_arm=1 pp_init=0 group_probe=1   (pp_init=0 -> S_POINTER=0 before,
# armed only by the broadcast, matching BSP DUMP_BEFORE_OPEN=0).
# Encoding (from cmac_open): low=(value<<16)|offset ; high=(routing<<24)|0x010000.
import sys, time, shutil
PATH="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
src=open(PATH).read()
if "HEAD_ARM:" in src:
    print("ALREADY PATCHED"); sys.exit(0)

# 1) param next to cmac_open
pa="static int cmac_open;\nmodule_param(cmac_open, int, 0644);"
if src.count(pa)!=1: print("param anchor count=%d abort"%src.count(pa)); sys.exit(1)
src=src.replace(pa, pa+"\nstatic int head_arm;\nmodule_param(head_arm, int, 0644);",1)

# 2) prepend block — insert BEFORE the cmac_open append block so the shift happens first
anchor=("\t\t\t/* 2026-05-30: cmac_open")
if src.count(anchor)!=1: print("block anchor count=%d abort"%src.count(anchor)); sys.exit(1)
blk="""\t\t\t/* HEAD-ARM 2026-06-19: prepend CNA/CMAC/CORE S_POINTER=0xe to regcmd head
\t\t\t * (broadcast), matching the BSP which mesa omits. Run with pp_init=0. */
\t\t\tif (head_arm) {
\t\t\t\tint oc = (int)task->regcmd_count;
\t\t\t\tmemmove(&p[6], &p[0], (size_t)oc * 2 * sizeof(u32));
\t\t\t\tp[0]=(0xeu<<16)|0x1004; p[1]=(0x02u<<24)|0x010000; /* CNA  S_POINTER */
\t\t\t\tp[2]=(0xeu<<16)|0x2004; p[3]=(0x04u<<24)|0x010000; /* CMAC S_POINTER */
\t\t\t\tp[4]=(0xeu<<16)|0x3004; p[5]=(0x08u<<24)|0x010000; /* CORE S_POINTER */
\t\t\t\ttask->regcmd_count = oc + 3;
\t\t\t\tflush_dcache_page(page);
\t\t\t\tdev_err(core->dev, "HEAD_ARM: prepended CNA/CMAC/CORE S_POINTER=0xe at head, count=%u",
\t\t\t\t\ttask->regcmd_count);
\t\t\t}
"""
src=src.replace(anchor, blk+anchor, 1)

shutil.copy(PATH, PATH+".bak-headarm-"+time.strftime("%Y%m%d-%H%M%S"))
open(PATH,"w").write(src)
print("PATCHED OK")
