"""
Revert rocket_job.c IOMMU_EXT diagnostic block back to read-only.

The AUTO_GATING write has been promoted to rockchip-iommu.c proper. Keep the
rocket-side read as verification: after the kernel patch, AUTO_GATING should
read back as 0x2 every PRE_OP (not 0x3 — that would mean the kernel write
didn't stick or got overwritten by FORCE_RESET).
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

old = (
    '\t\t/* IOMMU_EXT: read AUTO_GATING, write 0 to disable page-walker auto-gating,\n'
    '\t\t * then re-read to confirm. The mainline rockchip-iommu driver never writes\n'
    '\t\t * AUTO_GATING — reset default is 0x3 (gating enabled). Hypothesis: gated\n'
    '\t\t * page walker clock prevents the walker AXI port from completing TLB-miss\n'
    '\t\t * reads, producing the perpetual STATUS=0x01 stall. */\n'
    '\t\t{\n'
    '\t\t\tvoid __iomem *_mmio = ioremap(0xfde4b000, 0x100);\n'
    '\t\t\tif (_mmio) {\n'
    '\t\t\t\tu32 ag_pre = readl(_mmio + 0x24);\n'
    '\t\t\t\tu32 im     = readl(_mmio + 0x1C);\n'
    '\t\t\t\tu32 is     = readl(_mmio + 0x18);\n'
    '\t\t\t\twritel(0x2, _mmio + 0x24);\n'
    '\t\t\t\t{ u32 ag_post = readl(_mmio + 0x24);\n'
    '\t\t\t\t  dev_err(core->dev,\n'
    '\t\t\t\t\t"IOMMU_EXT: AUTO_GATING pre=0x%08x wrote=0x2 post=0x%08x INT_MASK=0x%08x INT_STATUS=0x%08x",\n'
    '\t\t\t\t\tag_pre, ag_post, im, is);\n'
    '\t\t\t\t}\n'
    '\t\t\t\tiounmap(_mmio);\n'
    '\t\t\t} else {\n'
    '\t\t\t\tdev_err(core->dev, "IOMMU_EXT: ioremap(0xfde4b000) failed");\n'
    '\t\t\t}\n'
    '\t\t}\n'
)
new = (
    '\t\t/* IOMMU_EXT (read-only): verify kernel-level AUTO_GATING fix stuck.\n'
    '\t\t * Expected AUTO_GATING=0x2 after rockchip-iommu.c writes the value in\n'
    '\t\t * rk_iommu_enable() and rk_iommu_resume_paging(). 0x3 means stale. */\n'
    '\t\t{\n'
    '\t\t\tvoid __iomem *_mmio = ioremap(0xfde4b000, 0x100);\n'
    '\t\t\tif (_mmio) {\n'
    '\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t"IOMMU_EXT: AUTO_GATING=0x%08x INT_MASK=0x%08x INT_STATUS=0x%08x",\n'
    '\t\t\t\t\treadl(_mmio + 0x24),\n'
    '\t\t\t\t\treadl(_mmio + 0x1C),\n'
    '\t\t\t\t\treadl(_mmio + 0x18));\n'
    '\t\t\t\tiounmap(_mmio);\n'
    '\t\t\t} else {\n'
    '\t\t\t\tdev_err(core->dev, "IOMMU_EXT: ioremap(0xfde4b000) failed");\n'
    '\t\t\t}\n'
    '\t\t}\n'
)

if old not in src:
    print('Anchor NOT FOUND', file=sys.stderr); sys.exit(1)

src = src.replace(old, new, 1)
with open(PATH, 'w') as f:
    f.write(src)
print(f'Wrote {PATH}')
