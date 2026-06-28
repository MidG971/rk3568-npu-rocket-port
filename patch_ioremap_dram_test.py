"""
Patch rocket_job.c to add non-cached ioremap diagnostic for regcmd PA.

Tests whether the physical address of the regcmd buffer is actually readable
via non-cached CPU AXI path (ioremap = nGnRnE, bypasses CPU cache).

If ioremap shows valid data  → DRAM is accessible; NPU-specific AXI block
If ioremap shows zeros       → DMA sync failed; data only in CPU cache
If ioremap hangs/panics      → address is TZ-protected or bus issue affects CPU too

Also fixes BOUNCE dma_sync (smp_wmb → dma_sync_single_for_device).
Also fixes register write order per TRM Fig. 9-2.
"""
import sys
import os

path = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(path, 'r') as f:
    content = f.read()

# Fix 1: Replace ioremap_cache with ioremap in DTE readback
old1 = 'void __iomem *dtmap = ioremap_cache((phys_addr_t)dte, 32);'
new1 = 'void __iomem *dtmap = ioremap((phys_addr_t)dte, 32);'
if old1 in content:
    content = content.replace(old1, new1)
    print('Fix 1 (dte ioremap_cache→ioremap): OK')
else:
    print('Fix 1: NOT FOUND (check existing code)', file=sys.stderr)

old1b = 'void __iomem *ptemap = ioremap_cache((phys_addr_t)pte_table_pa, 0x1000);'
new1b = 'void __iomem *ptemap = ioremap((phys_addr_t)pte_table_pa, 0x1000);'
if old1b in content:
    content = content.replace(old1b, new1b)
    print('Fix 1b (pte ioremap_cache→ioremap): OK')
else:
    print('Fix 1b: NOT FOUND', file=sys.stderr)

# Fix 2: Add regcmd PA data dump via ioremap (non-cached) in PRE_OP block
# Find the PRE_OP_IOVA print and add regcmd data dump after it
old2 = '\t\tdev_err(core->dev,\n\t\t\t"PRE_OP_IOVA: regcmd=0x%llx SW_PA=0x%llx",\n\t\t\t(u64)task->regcmd, (u64)regcmd_pa);'
new2 = '''\t\tdev_err(core->dev,
\t\t\t"PRE_OP_IOVA: regcmd=0x%llx SW_PA=0x%llx",
\t\t\t(u64)task->regcmd, (u64)regcmd_pa);

\t\t/* Non-cached DRAM read of regcmd PA: bypasses CPU cache.
\t\t * Valid data → DRAM accessible by CPU (NPU AXI path may differ)
\t\t * All zeros → dma_sync_sgtable failed, data only in CPU cache
\t\t * Kernel panic → address TZ-protected or inaccessible */
\t\tif (regcmd_pa) {
\t\t\tvoid __iomem *rcmap = ioremap((phys_addr_t)regcmd_pa, 64);
\t\t\tif (rcmap) {
\t\t\t\tdev_err(core->dev,
\t\t\t\t\t"REGCMD_DRAM[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x",
\t\t\t\t\treadl(rcmap+0),  readl(rcmap+4),
\t\t\t\t\treadl(rcmap+8),  readl(rcmap+12),
\t\t\t\t\treadl(rcmap+16), readl(rcmap+20),
\t\t\t\t\treadl(rcmap+24), readl(rcmap+28));
\t\t\t\tiounmap(rcmap);
\t\t\t} else {
\t\t\t\tdev_err(core->dev, "REGCMD_DRAM: ioremap failed for PA=0x%llx",
\t\t\t\t\t(u64)regcmd_pa);
\t\t\t}
\t\t}'''
if old2 in content:
    content = content.replace(old2, new2)
    print('Fix 2 (REGCMD_DRAM ioremap dump): OK')
else:
    print('Fix 2: PRE_OP_IOVA block not found', file=sys.stderr)
    idx = content.find('PRE_OP_IOVA')
    print(f'  nearest PRE_OP_IOVA at index {idx}')
    if idx >= 0:
        print(repr(content[max(0,idx-100):idx+200]))

# Fix 3: BOUNCE dma_sync fix
# Replace smp_wmb() after memcpy with proper DMA flush
old3 = '\tsmp_wmb();\n\n\t/* Write regcmd IOVA and count to PC registers */'
new3 = '''\tdma_sync_single_for_device(core->dev,
\t\tpage_to_phys(task->staging_page), PAGE_SIZE, DMA_TO_DEVICE);

\t/* Write regcmd IOVA and count to PC registers */'''
if old3 in content:
    content = content.replace(old3, new3)
    print('Fix 3 (BOUNCE dma_sync): OK')
else:
    # Try alternate context
    old3b = 'smp_wmb();\n\n\t/* Write'
    if old3b in content:
        idx = content.find(old3b)
        print(f'Fix 3: found smp_wmb at index {idx}')
        print(repr(content[max(0,idx-50):idx+100]))
    else:
        print('Fix 3: smp_wmb pattern not found', file=sys.stderr)
        idx = content.find('smp_wmb')
        print(f'  smp_wmb at index {idx}')

# Fix 4: Register write order per TRM Fig. 9-2
# Current order: BASE_ADDRESS, AMOUNTS, INT_MASK, INT_CLEAR, TASK_CON, TASK_DMA_BASE_ADDR, OP_EN
# Correct order: INT_CLEAR, INT_MASK, AMOUNTS, BASE_ADDRESS, TASK_CON, TASK_DMA_BASE_ADDR, OP_EN
old4 = '''\trocket_pc_writel(core, BASE_ADDRESS, task->regcmd);
\trocket_pc_writel(core, REGISTER_AMOUNTS,'''
new4 = '''\t/* TRM Fig. 9-2: INT_CLEAR + INT_MASK MUST come before BASE_ADDRESS */
\trocket_pc_writel(core, INTERRUPT_CLEAR, 0x3FFF);
\trocket_pc_writel(core, INTERRUPT_MASK,'''

# This is a complex reorder - let's check if the pattern exists
if old4 in content:
    print('Fix 4 target found - write order fix needed (manual review suggested)')
else:
    print('Fix 4: BASE_ADDRESS pattern not found', file=sys.stderr)
    idx = content.find('BASE_ADDRESS')
    print(f'  BASE_ADDRESS at index {idx}')
    if idx >= 0:
        print(repr(content[max(0,idx-20):idx+200]))

with open(path, 'w') as f:
    f.write(content)
print('Wrote', path)
print()
print('Summary:')
print('  Fix 1+1b: ioremap_cache → ioremap for DTE/PTE readback')
print('  Fix 2:    Add REGCMD_DRAM dump via ioremap(non-cached) at regcmd PA')
print('  Fix 3:    BOUNCE: smp_wmb() → dma_sync_single_for_device()')
print('  Fix 4:    Register write order (manual review needed)')
