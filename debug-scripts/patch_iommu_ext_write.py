"""
Upgrade IOMMU_EXT block to also WRITE 0 to AUTO_GATING before OP_EN.

Observation: AUTO_GATING reads 0x00000003 — page walker clock-gating enabled
by hardware default. The mainline rockchip-iommu driver never writes this
register, so the default carries through into runtime. Hypothesis: when the
NPU PC issues an IOVA read, the IOMMU sees TLB miss, fires a page walk AXI
read, but the walker's clock is gated → AXI never completes → STATUS=0x01
stall.

Test: write 0 to AUTO_GATING right before NPU OP_EN. If the stall clears
and the job completes, AUTO_GATING is BUG A.

If confirmed, the proper fix is in rockchip-iommu.c (rk_iommu_enable and
rk_iommu_resume_paging) — not in the consumer driver.
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

old = (
    '\t\t/* IOMMU_EXT: read AUTO_GATING/INT_MASK/INT_STATUS via direct MMIO.\n'
    '\t\t * The mainline rockchip-iommu driver never writes AUTO_GATING — if its\n'
    '\t\t * reset value gates the page walker clock, that explains STATUS=0x01\n'
    '\t\t * AXI stall (walker AXI port loses clock between TLB miss requests). */\n'
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

new = (
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
    '\t\t\t\twritel(0, _mmio + 0x24);\n'
    '\t\t\t\t{ u32 ag_post = readl(_mmio + 0x24);\n'
    '\t\t\t\t  dev_err(core->dev,\n'
    '\t\t\t\t\t"IOMMU_EXT: AUTO_GATING pre=0x%08x wrote=0 post=0x%08x INT_MASK=0x%08x INT_STATUS=0x%08x",\n'
    '\t\t\t\t\tag_pre, ag_post, im, is);\n'
    '\t\t\t\t}\n'
    '\t\t\t\tiounmap(_mmio);\n'
    '\t\t\t} else {\n'
    '\t\t\t\tdev_err(core->dev, "IOMMU_EXT: ioremap(0xfde4b000) failed");\n'
    '\t\t\t}\n'
    '\t\t}\n'
)

if old not in src:
    print('Anchor NOT FOUND — has the earlier IOMMU_EXT patch been applied?', file=sys.stderr); sys.exit(1)
if 'AUTO_GATING pre=' in src:
    print('Already patched — skipping', file=sys.stderr); sys.exit(0)

src = src.replace(old, new, 1)

with open(PATH, 'w') as f:
    f.write(src)
print(f'Wrote {PATH}')
