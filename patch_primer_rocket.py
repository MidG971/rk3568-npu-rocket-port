#!/usr/bin/env python3
# BSP-primer setup: make rocket co-loadable with rknpu on ONE kernel + the rknpu (1-region) DT.
#  (1) rocket match table: also bind "rockchip,rk3568-rknpu"/"rockchip,rknpu" (the rknpu node).
#  (2) rocket reg: fall back to a SINGLE full region (pc/cna/core = base+0/0x1000/0x3000) when the
#      byname "pc" lookup fails — preserves the 3-region rocket DTB (l3) AND binds the 1-region rknpu DT.
import sys, time, shutil

CORE="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_core.c"
DRV="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_drv.c"

# --- (1) match table ---
d=open(DRV).read()
if "rk3568-rknpu" not in d:
    anc='\t{ .compatible = "rockchip,rk3568-rknn-core", .data = &rk3568_soc_data },'
    if d.count(anc)!=1: print("DRV match anchor count=%d abort"%d.count(anc)); sys.exit(1)
    d=d.replace(anc, anc+'\n\t{ .compatible = "rockchip,rk3568-rknpu", .data = &rk3568_soc_data }, /* PRIMER */\n\t{ .compatible = "rockchip,rknpu", .data = &rk3568_soc_data }, /* PRIMER */',1)
    shutil.copy(DRV, DRV+".bak-primer-"+time.strftime("%Y%m%d-%H%M%S"))
    open(DRV,"w").write(d); print("DRV match table PATCHED")
else:
    print("DRV already has rk3568-rknpu")

# --- (2) single-region reg fallback ---
c=open(CORE).read()
if "PRIMER: 1-region" in c:
    print("CORE already patched"); sys.exit(0)
old='''	core->pc_iomem = devm_platform_ioremap_resource_byname(pdev, "pc");
	if (IS_ERR(core->pc_iomem)) {
		dev_err(dev, "couldn't find PC registers %ld\\n", PTR_ERR(core->pc_iomem));
		return PTR_ERR(core->pc_iomem);
	}

	core->cna_iomem = devm_platform_ioremap_resource_byname(pdev, "cna");
	if (IS_ERR(core->cna_iomem)) {
		dev_err(dev, "couldn't find CNA registers %ld\\n", PTR_ERR(core->cna_iomem));
		return PTR_ERR(core->cna_iomem);
	}

	core->core_iomem = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(core->core_iomem)) {
		dev_err(dev, "couldn't find CORE registers %ld\\n", PTR_ERR(core->core_iomem));
		return PTR_ERR(core->core_iomem);
	}'''
new='''	core->pc_iomem = devm_platform_ioremap_resource_byname(pdev, "pc");
	if (IS_ERR(core->pc_iomem)) {
		/* PRIMER: 1-region rknpu DT fallback — map the full NPU, derive sub-bases. */
		void __iomem *base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(base)) {
			dev_err(dev, "couldn't find NPU registers %ld\\n", PTR_ERR(base));
			return PTR_ERR(base);
		}
		core->pc_iomem = base;
		core->cna_iomem = base + 0x1000;
		core->core_iomem = base + 0x3000;
		dev_info(dev, "PRIMER: single-region NPU map (pc/cna/core = base+0/0x1000/0x3000)\\n");
	} else {
		core->cna_iomem = devm_platform_ioremap_resource_byname(pdev, "cna");
		if (IS_ERR(core->cna_iomem))
			return PTR_ERR(core->cna_iomem);
		core->core_iomem = devm_platform_ioremap_resource_byname(pdev, "core");
		if (IS_ERR(core->core_iomem))
			return PTR_ERR(core->core_iomem);
	}'''
if c.count(old)!=1: print("CORE reg block anchor count=%d abort"%c.count(old)); sys.exit(1)
c=c.replace(old,new,1)
shutil.copy(CORE, CORE+".bak-primer-"+time.strftime("%Y%m%d-%H%M%S"))
open(CORE,"w").write(c); print("CORE single-region fallback PATCHED")
