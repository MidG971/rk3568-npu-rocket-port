"""
Test: write 0 to PC_TASK_DMA_BASE_ADDR instead of task->regcmd.

Hypothesis: rocket currently writes task->regcmd (the regcmd buffer IOVA) to
PC_TASK_DMA_BASE_ADDR. Per BSP rknpu_job.c (line 374), this register actually
takes a SEPARATE IOVA pointing at an array of struct rknpu_task descriptors
(regcfg_amount, int_mask, regcmd_addr). When PC reads the regcmd buffer
content as a task descriptor, fields are nonsense → PC fires mmu_error
(bit 14, sticky/unmaskable) at ~10ms and the job times out.

Test outcomes for write 0:
  A) PC ignores TASK_DMA_BASE_ADDR when TASK_NUMBER=1 → job may succeed
  B) PC issues AXI read to IOVA 0 → IOMMU PAGE_FAULT (visible in INT_STATUS/FA)
  C) Same mmu_error timeline → PC reads task DMA from somewhere else (BUG D)

Outcome B is most informative even if it doesn't fix the bug — it confirms PC
relies on this register and we need to allocate a real task descriptor.
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

old = '\trocket_pc_writel(core, TASK_DMA_BASE_ADDR, task->regcmd);\n'
new = (
    '\t/* TEST 2026-05-12 PM: PC_TASK_DMA_BASE_ADDR expects a SEPARATE task descriptor\n'
    '\t * table IOVA (struct rknpu_task[]), not the regcmd buffer. Writing the regcmd\n'
    '\t * here causes PC to interpret regcmd bytes as task descriptor fields → garbage\n'
    '\t * → mmu_error. Try 0 first to see if PC ignores it for TASK_NUMBER=1. */\n'
    '\trocket_pc_writel(core, TASK_DMA_BASE_ADDR, 0);\n'
)

if old not in src:
    print('Anchor NOT FOUND', file=sys.stderr); sys.exit(1)
src = src.replace(old, new, 1)
with open(PATH, 'w') as f:
    f.write(src)
print(f'Wrote {PATH}')
