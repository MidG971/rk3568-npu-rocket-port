#!/usr/bin/env python3
# Add `replay_raw` param that bypasses the entire debug regcmd-rewrite block
# (line ~543 "always patch regcmd"), so rocket submits task->regcmd UNMODIFIED.
# Essential for a faithful Method B replay of the vendor cmdstream.
import sys, time, shutil
PATH="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
src=open(PATH).read()
if "int replay_raw" in src:
    print("ALREADY PATCHED"); sys.exit(0)

# 1) param next to cmac_open
pa="static int cmac_open;\nmodule_param(cmac_open, int, 0644);"
if src.count(pa)!=1: print("param anchor count=%d abort"%src.count(pa)); sys.exit(1)
src=src.replace(pa, pa+"\nstatic int replay_raw;\nmodule_param(replay_raw, int, 0644);\n"
    'MODULE_PARM_DESC(replay_raw, "Bypass the debug regcmd-rewrite block; submit task->regcmd unmodified (1=on)");',1)

# 2) gate the block at line 543
old="\tif (core->attached_domain && core->attached_domain->domain) {  /* always patch regcmd */"
if src.count(old)!=1: print("block anchor count=%d abort"%src.count(old)); sys.exit(1)
new="\tif (!replay_raw && core->attached_domain && core->attached_domain->domain) {  /* always patch regcmd (gated by replay_raw) */"
src=src.replace(old,new,1)

shutil.copy(PATH, PATH+".bak-replayraw-"+time.strftime("%Y%m%d-%H%M%S"))
open(PATH,"w").write(src)
print("PATCHED OK")
