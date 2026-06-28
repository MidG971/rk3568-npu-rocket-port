#!/usr/bin/env python3
# gahingwoo 2026-06-18 engage recipe test on RK3568:
#  engage4 : append the 4 per-unit OP_EN regcmd entries (no PC restart)
#            CNA 0x1008/0x02, CORE 0x3008/0x08, DPU 0x4008/0x10, RDMA 0x5008/0x20, value 1
#  bcast_oe: append the broadcast OP_EN (target 0x81, reg 0x08, value 0x1d) [restarts PC]
# Encoding (from cmac_open): low=(value<<16)|offset ; high=(routing<<24)|0x010000
import sys, time, shutil
PATH="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
src=open(PATH).read()
if "ENGAGE4:" in src:
    print("ALREADY PATCHED"); sys.exit(0)

# 1) params after cmac_open
pa="static int cmac_open;\nmodule_param(cmac_open, int, 0644);"
if src.count(pa)!=1: print("param anchor count=%d abort"%src.count(pa)); sys.exit(1)
src=src.replace(pa, pa+"\n"
"static int engage4;\nmodule_param(engage4, int, 0644);\n"
"static int bcast_oe;\nmodule_param(bcast_oe, int, 0644);",1)

# 2) append blocks after the cmac_open block
anchor=('\t\t\t\tdev_err(core->dev,\n'
        '\t\t\t\t\t"CMAC_OPEN: appended CMAC OP_ENABLE at slot %d (count=%u)",\n'
        '\t\t\t\t\tidx, task->regcmd_count);\n'
        '\t\t\t}')
if src.count(anchor)!=1: print("block anchor count=%d abort"%src.count(anchor)); sys.exit(1)
blk=anchor+"""

			/* gahingwoo 2026-06-18: 4 per-unit OP_EN (no PC restart). */
			if (engage4) {
				int idx = (int)task->regcmd_count;
				p[idx*2]=(1u<<16)|0x1008; p[idx*2+1]=(0x02u<<24)|0x010000; idx++;
				p[idx*2]=(1u<<16)|0x3008; p[idx*2+1]=(0x08u<<24)|0x010000; idx++;
				p[idx*2]=(1u<<16)|0x4008; p[idx*2+1]=(0x10u<<24)|0x010000; idx++;
				p[idx*2]=(1u<<16)|0x5008; p[idx*2+1]=(0x20u<<24)|0x010000; idx++;
				task->regcmd_count = idx;
				flush_dcache_page(page);
				dev_err(core->dev, "ENGAGE4: appended per-unit OP_EN CNA/CORE/DPU/RDMA, count=%u",
					task->regcmd_count);
			}
			/* gahingwoo 2026-06-18: broadcast OP_EN target=0x81 reg=0x08 val=0x1d (restarts PC). */
			if (bcast_oe) {
				int idx = (int)task->regcmd_count;
				p[idx*2]=(0x1du<<16)|0x0008; p[idx*2+1]=(0x81u<<24)|0x010000; idx++;
				task->regcmd_count = idx;
				flush_dcache_page(page);
				dev_err(core->dev, "BCAST_OE: appended broadcast OP_EN 0x81/0x08=0x1d, count=%u",
					task->regcmd_count);
			}"""
src=src.replace(anchor, blk, 1)

shutil.copy(PATH, PATH+".bak-engage-"+time.strftime("%Y%m%d-%H%M%S"))
open(PATH,"w").write(src)
print("PATCHED OK")
