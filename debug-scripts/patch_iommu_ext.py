"""
Insert IOMMU_EXT diagnostic in rocket_job.c PRE_OP block.

Reads RK_MMU_AUTO_GATING (0x24), RK_MMU_INT_MASK (0x1C), RK_MMU_INT_STATUS (0x18)
directly via ioremap of the NPU IOMMU MMIO base (0xfde4b000 from DT).

Rationale: rockchip-iommu.c never writes AUTO_GATING. If reset default leaves
the page walker auto-clock-gated, the IOMMU's AXI master stalls (STATUS=0x01)
on TLB-miss page walks because the walker clock has gone idle. This passive
read tells us the current value; if non-zero, a kernel-side patch will write 0.
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

anchor = (
    '\t\tif (!IS_ERR_OR_NULL(pmu)) {\n'
    '\t\t\tregmap_read(pmu, 0x60, &bus_idle);\n'
    '\t\t\tregmap_read(pmu, 0x70, &noc_auto);\n'
    '\t\t}\n'
)
new = anchor + (
    '\n'
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

if anchor not in src:
    print('Anchor NOT FOUND', file=sys.stderr); sys.exit(1)
if 'IOMMU_EXT:' in src:
    print('Already patched (IOMMU_EXT present) — skipping', file=sys.stderr); sys.exit(0)

src = src.replace(anchor, new, 1)

with open(PATH, 'w') as f:
    f.write(src)
print(f'Wrote {PATH}')
