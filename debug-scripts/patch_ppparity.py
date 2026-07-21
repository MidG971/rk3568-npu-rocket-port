#!/usr/bin/env python3
# Splice a PP-bank parity readout into the group_probe block of rocket_job.c.
# Reads CNA DATA_SIZE0 (0x1020) under POINTER=0 and POINTER=1 to see which
# ping-pong group holds the conv geometry, plus the live consumer pointer.
import sys, time, shutil

PATH = "/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"

with open(PATH) as f:
    src = f.read()

if "PPPARITY:" in src:
    print("ALREADY PATCHED — nothing to do")
    sys.exit(0)

anchor = "\t\t\tiounmap(_gp);"
if src.count(anchor) != 1:
    print("ANCHOR NOT UNIQUE (count=%d) — aborting" % src.count(anchor))
    sys.exit(1)

block = (
"\t\t\t{\n"
"\t\t\t\tu32 sp_save = readl(_gp + 0x1004);\n"
"\t\t\t\tu32 ds0_g0, ds0_g1, sstat;\n"
"\t\t\t\twritel((sp_save & ~0x1u), _gp + 0x1004);          /* select PP group 0 */\n"
"\t\t\t\tds0_g0 = readl(_gp + 0x1020);\n"
"\t\t\t\twritel((sp_save & ~0x1u) | 0x1u, _gp + 0x1004);   /* select PP group 1 */\n"
"\t\t\t\tds0_g1 = readl(_gp + 0x1020);\n"
"\t\t\t\twritel(sp_save, _gp + 0x1004);                     /* restore */\n"
"\t\t\t\tsstat = readl(_gp + 0x1000);\n"
"\t\t\t\tdev_err(core->dev,\n"
"\t\t\t\t\t\"PPPARITY: sp_live=%08x ds0_g0=%08x ds0_g1=%08x sstat=%08x exec16=%u cons_ptr=%u\",\n"
"\t\t\t\t\tsp_save, ds0_g0, ds0_g1, sstat,\n"
"\t\t\t\t\t!!(sp_save & 0x10000), (sp_save & 0x1u));\n"
"\t\t\t}\n"
)

shutil.copy(PATH, PATH + ".bak-ppparity-" + time.strftime("%Y%m%d-%H%M%S"))
src = src.replace(anchor, block + anchor, 1)
with open(PATH, "w") as f:
    f.write(src)
print("PATCHED OK")
