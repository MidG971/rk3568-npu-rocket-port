# Debug Prints Removed During IOMMU Investigation

These prints were added during the IOMMU mode debugging session (2026-03-29 to 2026-03-31)
and removed once the PTE=0 bug was fixed. Kept here for reference in case they are needed again.

---

## drivers/rknpu/rknpu_iommu.c

### `rknpu_iommu_init_domain` — first implementation (multi-domain path, `#ifdef` branch)

```c
dev_warn(rknpu_dev->dev, "iommu_init_domain called: domains[0]=%p\n",
         rknpu_dev->iommu_domains[0]);
// ...
dev_warn(rknpu_dev->dev, "iommu_init_domain: set domains[0]=%p\n",
         rknpu_dev->iommu_domains[0]);
```

**What it showed**: Whether the domain pointer was NULL before init, and what it became after.

### `rknpu_iommu_init_domain` — second implementation (`#else` branch, active on RK3568)

```c
dev_warn(rknpu_dev->dev, "iommu_init_domain: domains[0]=%p type=%u dma_iommu=%d\n",
         rknpu_dev->iommu_domains[0],
         rknpu_dev->iommu_domains[0] ? rknpu_dev->iommu_domains[0]->type : 0xdeadU,
         rknpu_dev->dev->dma_iommu);
dev_warn(rknpu_dev->dev, "iommu_init_domain: dma_ops=%ps cookie_type=%d\n",
         get_dma_ops(rknpu_dev->dev),
         rknpu_dev->iommu_domains[0] ? (int)rknpu_dev->iommu_domains[0]->cookie_type : -1);
```

**What it showed**: Domain type (3 = DMA), `dma_iommu` flag (1 = IOMMU DMA ops active),
`dma_ops` (0x0 = iommu-dma), `cookie_type` (1 = IOMMU_DOMAIN_DMA). Confirmed IOMMU DMA
path was active at probe time.

---

## drivers/rknpu/rknpu_job.c

### `job_commit` — per-inference register dump

```c
LOG_DEV_WARN(rknpu_dev->dev,
    "job_commit core%d: regcmd_addr=0x%016llx task_base=0x%016llx "
    "int_mask=0x%08x task_ctrl=0x%08x data_amount=0x%08x\n",
    core_index, first_task->regcmd_addr, args->task_base_addr,
    last_task->int_mask,
    ((0x6 | task_pp_en) << pc_task_number_bits) | task_number,
    REG_READ(RKNPU_OFFSET_PC_DATA_AMOUNT));
```

**What it showed**: Per-job register values at OP_EN time. Confirmed that `regcmd_addr`
(0xff7f0000) matched the IOMMU fault address, proving the fault occurred during
command buffer fetch. `data_amount=0x87` was the command count.

### `post_op_en` — 100µs readback block (entire block removed including `udelay`)

```c
/* Post-trigger readback: check if NPU started */
{
    u32 raw_post, int_post;

    udelay(100);
    raw_post = REG_READ(RKNPU_OFFSET_INT_RAW_STATUS);
    int_post = REG_READ(RKNPU_OFFSET_INT_STATUS);
    LOG_DEV_WARN(rknpu_dev->dev,
        "post_op_en: RAW_STATUS=0x%08x INT_STATUS=0x%08x (100us after OP_EN)\n",
        raw_post, int_post);
}
```

**What it showed**: Whether the NPU started executing 100µs after OP_EN=1. With the
PTE=0 bug, RAW_STATUS=0 and INT_STATUS=0 (NPU stalled waiting for page table). After
fix, both are 0 (job completes normally before the 100µs).

---

## drivers/rknpu/rknpu_gem.c

### `gem_get_pages` — dma_iommu state dump (removed earlier)

```c
dev_warn(drm->dev, "gem_get_pages: dma_iommu=%d dma_ops=%ps domain_type=%d\n",
         drm->dev->dma_iommu, get_dma_ops(drm->dev),
         iommu_get_domain_for_dev(drm->dev) ?
         iommu_get_domain_for_dev(drm->dev)->type : -1);
```

### `gem_get_pages` — IOVA-to-phys verification (removed earlier)

```c
{
    struct iommu_domain *__dom = iommu_get_domain_for_dev(drm->dev);
    phys_addr_t __pa = __dom ? iommu_iova_to_phys(__dom, dma_addr) : 0;
    dev_warn(drm->dev,
             "gem_get_pages: dma_addr=%pad iova_to_phys=%pa size=%zu\n",
             &rknpu_obj->dma_addr, &__pa, rknpu_obj->size);
}
```

**What it showed**: Confirmed IOMMU was active and verified IOVA→phys mapping was
correct after dma_map. Without the GFP_DMA32 fix, `iova_to_phys` returned 0 (bad PTE).

### `gem_alloc_buf_with_cache` — allocation path traces (removed earlier)

```c
dev_warn(drm->dev, "gem_alloc: NON_CONTIGUOUS path (get_pages), flags=0x%x\n",
         rknpu_obj->flags);
// and:
dev_warn(drm->dev, "gem_alloc: dma_alloc_attrs path, flags=0x%x iommu_en=%d\n",
         rknpu_obj->flags, rknpu_dev->iommu_en);
```

**What it showed**: Which allocation path was taken (NON_CONTIGUOUS = scatter-gather
with IOMMU mapping, vs contiguous DMA alloc).

---

## drivers/iommu/rockchip-iommu.c

### `rk_iommu_irq` — hw_needs_enable gate status

```c
dev_info(iommu->dev, "rk_iommu_irq: hw_needs_enable=%d\n", iommu->hw_needs_enable);
```

**What it showed**: Whether an incoming IRQ would be gated (hw_needs_enable=1 → spurious,
return IRQ_NONE) or handled. Confirmed IRQ handling was active after trigger_enable.

### `rk_iommu_enable` — DTE_ADDR verification

```c
{
    u32 dte_val = rk_ops->mk_dtentries(rk_domain->dt_dma);
    phys_addr_t decoded = rk_ops->pt_address(dte_val);
    dev_info(iommu->dev,
        "rk_iommu_enable[%d]: domain=%p dt_dma=%pa mk=0x%08x decoded=%pa\n",
        i, iommu->domain, &rk_domain->dt_dma, dte_val, &decoded);
    rk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, dte_val);
}
```

**Cleanup note**: The inner block was collapsed back to a single line:
`rk_iommu_write(iommu->bases[i], RK_MMU_DTE_ADDR, rk_ops->mk_dtentries(rk_domain->dt_dma));`

**What it showed**: The DTE physical address written to the MMU register. With the
PTE=0 bug: `dt_dma=0xe02a0000` was the SWIOTLB bounce buffer address. After fix:
`dt_dma` is a normal <4GB address, `decoded` matches, confirming correct page table setup.

### `rk_iommu_resume` — deferred enable trace

```c
dev_info(dev, "rk_iommu_resume: calling rk_iommu_enable domain=%p\n", iommu->domain);
```

**What it showed**: Confirmed the resume path called rk_iommu_enable (not the deferred
path). Relevant when debugging PM suspend/resume interactions.

### `trigger_enable` — post-enable register dump

```c
dev_info(iommu_dev,
    "trigger_enable[%d]: DTE_ADDR=0x%08x STATUS=0x%08x INT_MASK=0x%08x\n",
    j,
    rk_iommu_read(iommu->bases[j], RK_MMU_DTE_ADDR),
    rk_iommu_read(iommu->bases[j], RK_MMU_STATUS),
    rk_iommu_read(iommu->bases[j], RK_MMU_INT_MASK));
```

**What it showed**: Hardware register state immediately after IOMMU was enabled via
`rk_iommu_trigger_enable()`. `STATUS=0x00000019` = paging enabled + stall active bits.
`INT_MASK=0x00000003` = fault + bus error IRQs unmasked.

---

## Also removed (earlier session)

### `rockchip-iommu.c` — `before_flush` / `after_flush` PTE verification

```c
pr_warn("rk_iommu: before_flush pte[0]=0x%llx (domain=%p)\n", *pte, domain);
// ... dma_sync_single_for_device() call ...
pr_warn("rk_iommu: after_flush pte[0]=0x%llx\n", *pte);
```

**What it showed**: Whether `dma_sync_single_for_device(DMA_TO_DEVICE)` was zeroing
PTEs. Before fix: `after_flush pte[0]=0x0` (SWIOTLB zeroed the PTE).
After fix: `after_flush pte[0]=0x49cc007` (preserved). This was the **key diagnostic**
that proved the SWIOTLB bounce buffer mechanism.

### `rockchip-iommu.c` — `rk_map` domain trace

```c
pr_warn("rk_iommu: rk_map: domain=%p iova=%lx paddr=%pa size=%zu\n",
        domain, iova, &paddr, size);
```

### `rknpu_gem.c` — `sg_dma_address` overwrite (bug fix, not just debug)

```c
// In rknpu_gem_alloc_buf_with_cache contiguous path:
sg_dma_address(s) = sg_phys(s);  // BUG: overwrote IOVA with phys addr
```

**This was a real bug** (not just a debug print): in the contiguous allocation path,
the BSP code overwrote the DMA-mapped IOVA with the physical address, causing incorrect
scatter-gather DMA addresses in IOMMU mode. Removed the overwrite line; the IOVA set
by `dma_map_sg` is now preserved.
