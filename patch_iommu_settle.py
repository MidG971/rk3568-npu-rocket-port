#!/usr/bin/env python3
# Test the source-diff hypothesis: BSP enables the IOMMU (trigger_enable) at power_on,
# well before OP_EN; rocket does iommu attach+trigger_enable+flush_tlb immediately before
# hw_submit. Add a settle delay between rocket's trigger_enable and hw_submit (param
# iommu_settle_ms) to replicate BSP's "IOMMU settled by submit time" separation.
import sys, time, shutil
PATH="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
src=open(PATH).read()
if "iommu_settle_ms" in src:
    print("ALREADY PATCHED"); sys.exit(0)

# 1) param next to cmac_open
pa="static int cmac_open;\nmodule_param(cmac_open, int, 0644);"
if src.count(pa)!=1: print("param anchor count=%d abort"%src.count(pa)); sys.exit(1)
src=src.replace(pa, pa+"\nstatic int iommu_settle_ms;\nmodule_param(iommu_settle_ms, int, 0644);\n"
    'MODULE_PARM_DESC(iommu_settle_ms, "msleep(ms) between IOMMU trigger_enable and hw_submit (BSP settles IOMMU at power_on)");',1)

# 2) settle delay right before hw_submit
anc='\tpr_emerg("ROCKET: before hw_submit\\n");'
if src.count(anc)!=1: print("hw_submit anchor count=%d abort"%src.count(anc)); sys.exit(1)
src=src.replace(anc, '\tif (iommu_settle_ms)\n\t\tmsleep(iommu_settle_ms); /* IOMMU-settle test */\n'+anc,1)

shutil.copy(PATH, PATH+".bak-settle-"+time.strftime("%Y%m%d-%H%M%S"))
open(PATH,"w").write(src)
print("PATCHED OK")
