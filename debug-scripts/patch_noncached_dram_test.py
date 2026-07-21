"""
Apply non-cached ioremap patches to rocket_job.c for BUG A diagnosis.

Changes:
  1. ioremap_cache → ioremap for DTE and PTE readback (get true DRAM value, bypass CPU cache)
  2. Add REGCMD_DRAM[0..7] dump via ioremap at regcmd physical PA
     - Valid data  → DRAM accessible by CPU; NPU AXI blockage is NPU-specific (firewall/TZ)
     - All zeros   → dma_sync failed; data only in CPU cache, not in DRAM
     - Panic/hang  → PA is TZ-protected; CPU AXI also blocked (DDR security firewall)
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

errors = 0

# Fix 1: ioremap_cache → ioremap for DTE
old = 'void __iomem *dtmap = ioremap_cache((phys_addr_t)dte, 32);'
new = 'void __iomem *dtmap = ioremap((phys_addr_t)dte, 32);'
if old in src:
    src = src.replace(old, new)
    print('Fix 1 (DTE ioremap_cache→ioremap): OK')
else:
    print('Fix 1: NOT FOUND', file=sys.stderr); errors += 1

# Fix 1b: ioremap_cache → ioremap for PTE
old = 'void __iomem *ptemap = ioremap_cache(\n\t\t\t\t\t\t(phys_addr_t)pte_table_pa, 0x1000);'
new = 'void __iomem *ptemap = ioremap(\n\t\t\t\t\t\t(phys_addr_t)pte_table_pa, 0x1000);'
if old in src:
    src = src.replace(old, new)
    print('Fix 1b (PTE ioremap_cache→ioremap): OK')
else:
    print('Fix 1b: NOT FOUND', file=sys.stderr); errors += 1

# Fix 2: Add REGCMD_DRAM[0..7] dump after PRE_OP_IOVA print
# Insert between PRE_OP_IOVA print and the blank lines before DTE block
old2 = ('\t\tdev_err(core->dev,\n'
        '\t\t\t"PRE_OP_IOVA: regcmd=0x%llx SW_PA=0x%llx",\n'
        '\t\t\t(u64)task->regcmd, (u64)regcmd_pa);\n'
        '\n'
        '\n'
        '\t\t/* Read DTE[0] from DRAM')
new2 = ('\t\tdev_err(core->dev,\n'
        '\t\t\t"PRE_OP_IOVA: regcmd=0x%llx SW_PA=0x%llx",\n'
        '\t\t\t(u64)task->regcmd, (u64)regcmd_pa);\n'
        '\n'
        '\t\t/* Non-cached CPU read of regcmd PA:\n'
        '\t\t * valid data -> DRAM accessible (NPU AXI may still be blocked by firewall)\n'
        '\t\t * all zeros  -> dma_sync_sgtable failed, data only in CPU cache\n'
        '\t\t * panic/hang -> PA is TZ-protected or inaccessible by both CPU and NPU */\n'
        '\t\tif (regcmd_pa) {\n'
        '\t\t\tvoid __iomem *rcmap = ioremap((phys_addr_t)regcmd_pa, 64);\n'
        '\t\t\tif (rcmap) {\n'
        '\t\t\t\tdev_err(core->dev,\n'
        '\t\t\t\t\t"REGCMD_DRAM[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x",\n'
        '\t\t\t\t\treadl(rcmap+0),  readl(rcmap+4),\n'
        '\t\t\t\t\treadl(rcmap+8),  readl(rcmap+12),\n'
        '\t\t\t\t\treadl(rcmap+16), readl(rcmap+20),\n'
        '\t\t\t\t\treadl(rcmap+24), readl(rcmap+28));\n'
        '\t\t\t\tiounmap(rcmap);\n'
        '\t\t\t} else {\n'
        '\t\t\t\tdev_err(core->dev, "REGCMD_DRAM: ioremap failed PA=0x%llx",\n'
        '\t\t\t\t\t(u64)regcmd_pa);\n'
        '\t\t\t}\n'
        '\t\t}\n'
        '\n'
        '\t\t/* Read DTE[0] from DRAM')

if old2 in src:
    src = src.replace(old2, new2)
    print('Fix 2 (REGCMD_DRAM ioremap dump): OK')
else:
    print('Fix 2: anchor not found', file=sys.stderr)
    idx = src.find('PRE_OP_IOVA')
    if idx >= 0:
        print(f'  PRE_OP_IOVA at index {idx}')
        print(repr(src[idx:idx+300]))
    errors += 1

with open(PATH, 'w') as f:
    f.write(src)

if errors:
    print(f'{errors} fix(es) failed — check above', file=sys.stderr)
    sys.exit(1)
print(f'Wrote {PATH}')
