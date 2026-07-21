#!/usr/bin/env python3
# Add a kernel-side NPU register dump at rocket PROBE (param `primediff`), for the cold-vs-primed diff.
# ioremap-based (safe, like rocket's other dumps). EXCLUDES 0xf000+ (that range hung the bus via /dev/mem).
import sys, time, shutil
P="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_core.c"
s=open(P).read()
if "PRIMEDIFF off=" in s:
    print("ALREADY PATCHED"); sys.exit(0)

# param
pa="static int arm_all;\nmodule_param(arm_all, int, 0644);"
if s.count(pa)!=1: print("param anchor count=%d abort"%s.count(pa)); sys.exit(1)
s=s.replace(pa, pa+"\nstatic int primediff;\nmodule_param(primediff, int, 0644);",1)

# dump block after PROBE_CORE
anc='''		dev_err(dev, "PROBE_CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
			readl(core->core_iomem + 0x00),
			readl(core->core_iomem + 0x04),
			readl(core->core_iomem + 0x08));'''
if s.count(anc)!=1: print("PROBE_CORE anchor count=%d abort"%s.count(anc)); sys.exit(1)
block=anc+'''

	if (primediff) {
		void __iomem *_pd = ioremap(0xfde40000, 0x10000);
		if (_pd) {
			static const u32 _po[] = {
				0x00,0x08,0x10,0x14,0x20,0x24,0x30,0x34,
				0x1000,0x1004,0x1008,0x100c,0x1010,0x1014,0x1018,0x1020,0x1024,
				0x1028,0x102c,0x1030,0x1034,0x1038,0x1040,0x1044,0x1090,0x1100,
				0x2000,0x2004,0x2008,0x200c,0x3000,0x3004,0x3008,0x300c,
				0x4000,0x4004,0x4008,0x400c,0x4020,0x4060,0x4070,0x4080,0x4084,0x40c0,
				0x5000,0x5004,0x5008,0x500c,0x5020,0x8010,0x8034,0x8038,0x803c };
			int _i;
			for (_i = 0; _i < ARRAY_SIZE(_po); _i++)
				dev_err(dev, "PRIMEDIFF off=0x%04x val=0x%08x",
					_po[_i], readl(_pd + _po[_i]));
			iounmap(_pd);
		}
	}'''
s=s.replace(anc, block, 1)
shutil.copy(P, P+".bak-primediff-"+time.strftime("%Y%m%d-%H%M%S"))
open(P,"w").write(s)
print("PATCHED OK")
