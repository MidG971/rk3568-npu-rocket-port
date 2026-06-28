"""
Replace ioremap(RAM_PA) with memremap(RAM_PA, size, MEMREMAP_WB) in
rocket_job.c's PRE_OP diagnostic block.

ioremap() on normal DRAM is rejected by Linux 6.x (WARN + NULL return).
memremap(MEMREMAP_WB) reuses the linear map (reads from CPU cache) without
kernel warnings. Also fixes read accessor: memremap returns void* not
void __iomem*, so use direct pointer casts instead of readl().
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

errors = 0

# Fix 1: REGCMD ioremap -> memremap
old1 = (
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
    '\t\t}'
)
new1 = (
    '\t\tif (regcmd_pa) {\n'
    '\t\t\tu32 *rcmap = memremap((phys_addr_t)regcmd_pa, 64, MEMREMAP_WB);\n'
    '\t\t\tif (rcmap) {\n'
    '\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t"REGCMD_DRAM[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x",\n'
    '\t\t\t\t\trcmap[0], rcmap[1], rcmap[2], rcmap[3],\n'
    '\t\t\t\t\trcmap[4], rcmap[5], rcmap[6], rcmap[7]);\n'
    '\t\t\t\tmemunmap(rcmap);\n'
    '\t\t\t} else {\n'
    '\t\t\t\tdev_err(core->dev, "REGCMD_DRAM: memremap failed PA=0x%llx",\n'
    '\t\t\t\t\t(u64)regcmd_pa);\n'
    '\t\t\t}\n'
    '\t\t}'
)
if old1 in src:
    src = src.replace(old1, new1, 1)
    print('Fix 1 (REGCMD ioremap->memremap): OK')
else:
    print('Fix 1: NOT FOUND', file=sys.stderr); errors += 1

# Fix 2: DTE ioremap -> memremap
old2 = (
    '\t\t/* Read DTE[0] from DRAM; then follow it to read the PTE entry */\n'
    '\t\tif (dte) {\n'
    '\t\t\tvoid __iomem *dtmap = ioremap((phys_addr_t)dte, 32);\n'
    '\t\t\tif (dtmap) {\n'
    '\t\t\t\tu32 dte0 = readl(dtmap + 0);\n'
    '\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t"DTE_DRAM: [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x",\n'
    '\t\t\t\t\tdte0, readl(dtmap + 4),\n'
    '\t\t\t\t\treadl(dtmap + 8), readl(dtmap + 12));\n'
    '\t\t\t\tiounmap(dtmap);\n'
    '\n'
    '\t\t\t\t/* Follow DTE[0] to read PTE entry for task->regcmd */\n'
    '\t\t\t\tif (dte0 & 1) {\n'
    '\t\t\t\t\tu32 pte_table_pa = dte0 & ~0x3FFu;\n'
    '\t\t\t\t\tu32 pte_idx = (task->regcmd >> 12) & 0x3FF;\n'
    '\t\t\t\t\tvoid __iomem *ptemap = ioremap(\n'
    '\t\t\t\t\t\t(phys_addr_t)pte_table_pa, 0x1000);\n'
    '\t\t\t\t\tif (ptemap) {\n'
    '\t\t\t\t\t\tu32 pte_val = readl(ptemap + pte_idx * 4);\n'
    '\t\t\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t\t\t"PTE_DRAM: table=0x%08x idx=0x%03x PTE=0x%08x (expected_pa=0x%llx)",\n'
    '\t\t\t\t\t\t\tpte_table_pa, pte_idx, pte_val,\n'
    '\t\t\t\t\t\t\t(u64)regcmd_pa);\n'
    '\t\t\t\t\t\tiounmap(ptemap);\n'
    '\t\t\t\t\t}\n'
    '\t\t\t\t}\n'
    '\t\t\t}\n'
    '\t\t}'
)
new2 = (
    '\t\t/* Read DTE[0] from DRAM via CPU (memremap=cached linear map);\n'
    '\t\t * then follow the DTE to read the PTE entry for task->regcmd.\n'
    '\t\t * If DTE/PTE are valid but IOMMU HW walk still stalls -> AXI bus issue. */\n'
    '\t\tif (dte) {\n'
    '\t\t\tu32 *dtmap = memremap((phys_addr_t)dte, 32, MEMREMAP_WB);\n'
    '\t\t\tif (dtmap) {\n'
    '\t\t\t\tu32 dte0 = dtmap[0];\n'
    '\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t"DTE_DRAM: [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x",\n'
    '\t\t\t\t\tdte0, dtmap[1], dtmap[2], dtmap[3]);\n'
    '\t\t\t\tmemunmap(dtmap);\n'
    '\n'
    '\t\t\t\t/* Follow DTE[0] to read PTE entry for task->regcmd */\n'
    '\t\t\t\tif (dte0 & 1) {\n'
    '\t\t\t\t\tu32 pte_table_pa = dte0 & ~0x3FFu;\n'
    '\t\t\t\t\tu32 pte_idx = (task->regcmd >> 12) & 0x3FF;\n'
    '\t\t\t\t\tu32 *ptemap = memremap(\n'
    '\t\t\t\t\t\t(phys_addr_t)pte_table_pa, 0x1000, MEMREMAP_WB);\n'
    '\t\t\t\t\tif (ptemap) {\n'
    '\t\t\t\t\t\tu32 pte_val = ptemap[pte_idx];\n'
    '\t\t\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t\t\t"PTE_DRAM: table=0x%08x idx=0x%03x PTE=0x%08x (expected_pa=0x%llx)",\n'
    '\t\t\t\t\t\t\tpte_table_pa, pte_idx, pte_val,\n'
    '\t\t\t\t\t\t\t(u64)regcmd_pa);\n'
    '\t\t\t\t\t\tmemunmap(ptemap);\n'
    '\t\t\t\t\t} else {\n'
    '\t\t\t\t\t\tdev_err(core->dev,\n'
    '\t\t\t\t\t\t\t"PTE_DRAM: memremap failed table=0x%08x",\n'
    '\t\t\t\t\t\t\tpte_table_pa);\n'
    '\t\t\t\t\t}\n'
    '\t\t\t\t} else {\n'
    '\t\t\t\t\tdev_err(core->dev, "DTE[0]=0x%08x not valid (bit0=0)", dte0);\n'
    '\t\t\t\t}\n'
    '\t\t\t} else {\n'
    '\t\t\t\tdev_err(core->dev, "DTE_DRAM: memremap failed PA=0x%08x", dte);\n'
    '\t\t\t}\n'
    '\t\t}'
)
if old2 in src:
    src = src.replace(old2, new2, 1)
    print('Fix 2 (DTE/PTE ioremap->memremap): OK')
else:
    print('Fix 2: NOT FOUND', file=sys.stderr); errors += 1
    idx = src.find('Read DTE[0] from DRAM')
    if idx >= 0:
        print(repr(src[idx:idx+300]))

with open(PATH, 'w') as f:
    f.write(src)

if errors:
    print(f'{errors} fix(es) failed', file=sys.stderr)
    sys.exit(1)
print(f'Wrote {PATH}')
