# Rocket NPU Driver Port to RK3568 — Status Report (2026-05-29)

## TL;DR

After 12 days of investigation, we have:

- **Kernel infrastructure: WORKING.** Rocket driver loads, IOMMU configured correctly, BOs allocated at BSP-equivalent IOVA layout, zero page faults, kernel stable through repeated job submissions.
- **PC (Program Controller) state machine: FULLY UNDERSTOOD.** PC reaches BSP-equivalent "completed" state (`TASKST=0x11000`) with proper `TASK_CON.task_number ≥ 6`.
- **Conv execution: NOT YET FIRING.** Sub-units (CNA/CMAC/ACCU/DPU/DPU_RDMA) have configs and OP_ENABLE values matching BSP byte-for-byte at MMIO, but `0x_008` run-status registers stay 0, no IRQ fires, output is uniform zero_point.

## Foundational mesa rocket bugs identified for RK3568

These would need to be fixed in `mesa/src/gallium/drivers/rocket/` for ANY RK3568 NPU usage via teflon. Each is testable independently:

### Bug 1: OP_ENABLE register offsets

| Sub-unit | Mesa offset | Correct offset (BSP) | Notes |
|----------|-------------|----------------------|-------|
| CNA | 0x1008 | (none — PC manages) | Drop the emit |
| CMAC | (not emitted) | 0x200c | Add emit with value 1 |
| ACCU | 0x3008 | 0x300c | Value: 0 (per BSP) |
| DPU | 0x4008 | 0x400c | Value: 0x108 (per BSP, mesa coincidentally writes this at 0x400c too) |
| DPU_RDMA | 0x5008 | 0x500c | Value: 0x13f for YOLO, 0x6f for MobileNet |

Mesa writes `OP_ENABLE = 1` at offset `xxxx008` for each sub-unit. BSP never writes anything at `xxxx008` — sub-unit OP_ENABLE registers are at `xxxx00c` with multi-bit values, not bit-0 booleans.

### Bug 2: CONV_CON1 missing from emit path

BSP writes `0x100c = 0xa000` (CONV_CON1). Same value across YOLOv5s and MobileNet — appears to be a "conv mode enable" constant. Mesa writes 0. Without this, CNA's pipeline doesn't engage.

### Bug 3: CBUF_CON0 wrong value

BSP writes `0x1040 = 0x17`. Mesa writes `0x44` (the HW default = effective no-op). The 0x17 likely encodes CBUF mode/size flags required for CNA's convolution buffer management.

### Bug 4: CONV_CON4 missing register definition (already in our local mesa patch)

Mesa's `registers.xml` did not define `CONV_CON4` (0x1018). BSP writes this register with conv-shape-specific values. Our local mesa patch adds the definition and the emit. Without it, CNA cannot enter its EXECUTER state for any non-RGB conv.

### Bug 5: DATA_SIZE1 wrong format

BSP YOLOv5s AND BSP MobileNet both write `0x1024 = 0x00020008` — constant across radically different conv shapes. Mesa writes `0xf0010` — different format entirely. This is a "constant config" register mesa appears to derive from input channel count, but BSP treats as fixed.

### Bug 6: DATA_SIZE3 wrong value

BSP YOLOv5s `0x102c = 0x1b80`, BSP MobileNet `0x102c = 0x1b90` — near-constant. Mesa writes `0x640` (1600). Likely a cycle-budget or timing-related fixed config.

### Bug 7: Missing task-descriptor BO architecture

This is in the kernel driver, not mesa. BSP's `rknpu_job.c::rknpu_job_subcore_commit_pc` writes `PC_DMA_BASE_ADDR = args->task_base_addr` (a separate IOVA pointing to an array of `struct rknpu_task` descriptors). Rocket never wrote this register. Without it, PC's TASK_DMA engine reads garbage and PC's task counter never advances.

Kernel fix implemented in `rocket_job.c` and `rocket_core.c`:
- Allocate 1 MB BO via `__get_free_pages(GFP_DMA32 | GFP_KERNEL, 8)` on first submit
- Place `struct rknpu_task` at offset 0xc0000 from BO base (PC prefetches at desc-0x80000 and desc-0xc0000; offset 0xc0000 places those reads inside the mapped 1MB region)
- Populate descriptor: regcmd_addr = task->regcmd, regcfg_amount = task->regcmd_count, int_mask = DPU_0|DPU_1
- Sync via `dma_sync_single_for_device`
- Write IOVA to `PC_DMA_BASE_ADDR`

### Bug 8: TASK_CON.task_number must be ≥ 6 for RK3568

Mesa sets `task_number = 1`. PC's task-counter state machine stalls at `TASKST=0xf000` waiting for completion signal. With `task_number ≥ 6` (BSP's MobileNet value), PC advances to `0x11000` (completed) within 1ms. This is RK3568-specific behavior; RK3588 may not have this requirement.

## Kernel infrastructure improvements (upstreamable independently)

### Fix 1: `rockchip-iommu.c` AUTO_GATING (BUG A)

The vendor RKNPU driver writes `RK_MMU_AUTO_GATING = 0x2` after `rk_iommu_enable_paging()` in both `rk_iommu_enable()` and `rk_iommu_resume_paging()`. Mainline driver never wrote this register; the HW default (0x3) leaves bit 1 set (page-walker auto-clock-gating), which causes the walker's AXI master to lose clock between transactions. On TLB miss → walker tries to fetch DTE → AXI never completes → infinite STATUS=0x01 (PAGING_ENABLED, no IDLE) for 500ms job timeout.

One-line fix affects all RK356x boards using IOMMU v1 (rknpu_mmu, vicap_mmu).

### Fix 2: IOMMU domain reuse (Option A v3)

Original rocket allocates a fresh `UNMANAGED` domain via `iommu_paging_domain_alloc(dev)`. BSP uses the device's default DMA domain (`iommu_get_domain_for_dev(dev)`), which has `iova_cookie` set by the dma-iommu framework. UNMANAGED domains miss TLB-priming optimizations, causing burst-fault on PC's 128-byte AXI bursts.

Fix: reuse the device's existing DMA domain via `iommu_get_domain_for_dev()`. Track with a `reused` flag in `struct rocket_iommu_domain` to skip `iommu_domain_free` for the borrowed domain.

### Fix 3: BO IOVA allocation via cookie iovad (Option B2)

Original rocket allocates BO IOVAs via `drm_mm` from a 0x10000-based fresh address space. BSP's task descriptor BOs land at high IOVAs (`0xfff*`). Mismatch causes PC's address-decoder to misinterpret some IOVA bits as page-fault-trigger flags.

Fix: allocate BO IOVAs via `alloc_iova_fast()` from the iova_cookie's iovad (the same allocator dma-iommu uses). All BOs then land at high IOVAs matching BSP pattern.

### Fix 4: NPU buffer allocation pattern (PA below 4GB)

`dma_alloc_coherent()` through dma-iommu silently ignores `GFP_DMA32` even with a 32-bit dma_mask set on the device. Pages are picked from `alloc_pages` without zone constraint enforcement; IOVA is constrained but PA isn't. NPU's 32-bit AXI master truncates high bits, so descriptor reads land at random PAs.

Correct pattern for kernel buffers the NPU reads:

```c
unsigned long page_ptr = __get_free_pages(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO, order);
dma_addr_t iova = dma_map_single(dev, (void *)page_ptr, size, dir);
/* PA guaranteed below 4GB; IOVA in device's IOMMU domain */
```

This pattern is documented in [[dma-alloc-pa-above-4gb-2026-05-28]] memory file.

### Fix 5: Rocket driver RK3568 binding (existing 5-patch series)

Already prepared and ready in `patches/rocket-rk3568/`:
1. `for_each_of_allnodes` → `of_find_matching_node` (module fix)
2. `rk3568_noc_init()` + `rk3568_soc_data`
3. Binding: add rk3568-rknn-core
4. rk356x-base.dtsi: Rocket node layout
5. rock-3b.dts: npu-supply, drop operating-points-v2

## Remaining unsolved problem

**Sub-unit activation does not fire.** All MMIO state matches BSP byte-for-byte:
- CNA: CONV_CON1=0xa000, CONV_CON2=0x30080, CONV_CON4=0x19000, all DATA_SIZE/WEIGHT_SIZE, CBUF_CON0=0x17 — all visible at MMIO
- CMAC: 0x200c=1 ✓
- ACCU: 0x300c=0 ✓
- DPU: 0x400c=0x108 ✓
- DPU_RDMA: 0x500c=0x13f ✓
- PC: TASKST=0x11000 (BSP completed state) ✓

Yet:
- CMAC[0x2008]=0, ACCU[0x3008]=0, DPU[0x4008]=0, DPU_RDMA[0x5008]=0, CNA[0x1008]=0 (none running)
- IRQ count = 0
- Output = uniform zero_point (DPU never wrote real data)
- No IOMMU bus activity (no AXI reads from sub-units)

Hypotheses still untested:
- Subtle conv-shape-specific value encoding bug in CONV_CON2 or DATA_SIZE0/2 or WEIGHT_SIZE
- HW-internal precondition not visible at MMIO (ping-pong group sync flags, MAC array enable, etc.)
- Some chip-wide enable register outside the NPU MMIO region (CRU/GRF/NoC)
- DPU/DPU_RDMA missing some of the 30 BSP-only register writes (we tried the non-zero subset, no help)

## Suggested upstream submission order

1. **`rockchip-iommu.c` AUTO_GATING patch** — standalone fix, affects multiple RK356x IOMMU users beyond rocket. Lowest-risk submission.
2. **Rocket 5-patch series** — refresh and submit, even with conv-not-yet-firing noted in cover letter. Maintainer (Tomeu) may have insight on the remaining bug.
3. **Mesa OP_ENABLE offset bug** — surgical mesa patch; share as RFC with Tomeu on dri-devel/freedesktop. Even if not sufficient for full functionality, foundationally correct.
4. **Task-descriptor BO architecture** — depends on rocket driver in mainline first. Submit after #2 lands.

## Memory artifacts (chronological)

`/home/dio/.claude/projects/-home-dio-Downloads-claude-npu-rock3b-npu-project/memory/`:

Most relevant for upstream prep:
- `op_en_offset_breakthrough_2026_05_28.md` — the wrong-offset bug
- `task_desc_v4_complete_2026_05_28.md` — task-descriptor BO architecture
- `dma_alloc_pa_above_4gb_2026_05_28.md` — buffer-allocation pattern
- `task_number_gates_pc_state_2026_05_29.md` — PC state machine
- `execution_doesnt_fire_2026_05_29.md` — what didn't work
- `bsp_cna_state_compared_2026_05_27.md` — BSP/rocket state comparison

## Live state on board

`/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c` has all 12 days of patches preserved in source. Module loads clean, kernel stable, all MMIO state matches BSP, zero page faults. The setup is reproducible and the gap is now narrow.
