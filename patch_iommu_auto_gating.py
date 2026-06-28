"""
Proper fix for BUG A (IOMMU AXI stall on RK3568 NPU IOMMU v1).

Root cause: RK_MMU_AUTO_GATING register reset value is 0x3 — both the
register-port and page-walker auto-gating bits are SET. When the page walker's
clock auto-gates, TLB-miss page walks issued by the NPU PC stall on the AXI
bus (STATUS=0x01) and never complete. The IOMMU driver never wrote this
register, so the hardware default carried through into runtime.

Fix: write 0x2 to AUTO_GATING (bit0=0 disables PAGE_WALKER auto-gating;
bit1=1 keeps slave/register-port gating — clearing bit1 resets DTE_ADDR).
Empirically verified from the rocket consumer driver: with AUTO_GATING=0x2,
IOMMU STATUS stays 0x19 (IDLE+PAGING_ENABLED+REPLAY_EMPTY) throughout the
job, no AXI stall.

Two write sites: rk_iommu_enable (full enable path) and rk_iommu_resume_paging
(post-rocket-reset path that skips force_reset).
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
    '\t}\n'
)
new1 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t\t/* AUTO_GATING default = 0x3; bit1 (page-walker auto-gate) causes\n'
    '\t\t * STATUS=0x01 AXI stall on TLB-miss page walks (BUG A). Clear bit1,\n'
    '\t\t * keep bit0 (register-port gate) — clearing bit0 resets DTE_ADDR. */\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
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
    '\t}\n'
)
new2 = (
    '\tfor (i = 0; i < iommu->num_mmu; i++) {\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_DISABLE_STALL);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR,\n'
    '\t\t\t       rk_ops->mk_dtentries(rk_domain->dt_dma));\n'
    '\t\trk_iommu_base_command(iommu->bases[i], RK_MMU_CMD_ZAP_CACHE);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_INT_MASK, RK_MMU_IRQ_MASK);\n'
    '\t\trk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);\n'
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
