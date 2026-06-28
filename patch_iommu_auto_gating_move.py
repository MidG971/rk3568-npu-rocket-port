"""
Move AUTO_GATING=0x2 write from BEFORE rk_iommu_enable_paging() to AFTER.

Empirical finding: writing AUTO_GATING in the same loop as DTE_ADDR/INT_MASK
(before enable_paging) leaves the IOMMU with paging disabled — STATUS bit 0
(PAGING_ENABLED) reads back as 0. Writing the same value AFTER enable_paging
preserves paging (rocket.ko's pre-OP write proved this).

Possible cause: in v1 IOMMU IP, AUTO_GATING latches its effect on the next
PAGING_ENABLE command. Writing AUTO_GATING before ENABLE_PAGING may make the
ENABLE_PAGING command see a gated walker → command never lands.

Fix: write AUTO_GATING right after rk_iommu_enable_paging() succeeds, in both
rk_iommu_enable() and rk_iommu_resume_paging().
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/iommu/rockchip-iommu.c'
with open(PATH) as f:
    src = f.read()

errors = 0

# Site 1: rk_iommu_enable
old1 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t\t/* AUTO_GATING default = 0x3; bit1 (page-walker auto-gate) causes\n'
    '\t\t * STATUS=0x01 AXI stall on TLB-miss page walks (BUG A). Clear bit1,\n'
    '\t\t * keep bit0 (register-port gate) — clearing bit0 resets DTE_ADDR. */\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
    '\t}\n'
    '\n'
    '\tret = rk_iommu_enable_paging(iommu);\n'
)
new1 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t}\n'
    '\n'
    '\tret = rk_iommu_enable_paging(iommu);\n'
    '\t/* AUTO_GATING default = 0x3; bit1 (page-walker auto-gate) causes\n'
    '\t * STATUS=0x01 AXI stall on TLB-miss page walks (BUG A). Clear bit1,\n'
    '\t * keep bit0 (register-port gate) — clearing bit0 resets DTE_ADDR.\n'
    '\t * MUST be written AFTER enable_paging — earlier writes leave paging\n'
    '\t * disabled (STATUS bit0 reads back as 0). */\n'
    '\tif (!ret) {\n'
    '\t\tfor (i = 0; i < iommu->num_mmu; i++)\n'
    '\t\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
    '\t}\n'
)
if old1 in src:
    src = src.replace(old1, new1, 1)
    print('Fix 1 (rk_iommu_enable): OK')
else:
    print('Fix 1: NOT FOUND', file=sys.stderr); errors += 1

# Site 2: rk_iommu_resume_paging
old2 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_DISABLE_STALL);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR,\n'
    '\t\t\t       rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
    '\t}\n'
    '\n'
    '\tret = rk_iommu_enable_paging(iommu);\n'
)
new2 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_DISABLE_STALL);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR,\n'
    '\t\t\t       rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t}\n'
    '\n'
    '\tret = rk_iommu_enable_paging(iommu);\n'
    '\t/* Disable page-walker auto-gating (BUG A) — AFTER enable_paging only. */\n'
    '\tif (!ret) {\n'
    '\t\tfor (i = 0; i < iommu->num_mmu; i++)\n'
    '\t\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
    '\t}\n'
)
if old2 in src:
    src = src.replace(old2, new2, 1)
    print('Fix 2 (rk_iommu_resume_paging): OK')
else:
    print('Fix 2: NOT FOUND', file=sys.stderr); errors += 1

with open(PATH, 'w') as f:
    f.write(src)

if errors:
    print(f'{errors} fix(es) failed', file=sys.stderr)
    sys.exit(1)
print(f'Wrote {PATH}')
