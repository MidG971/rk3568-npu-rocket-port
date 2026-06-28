# RK3568 Rocket NPU Driver Port — Peer Review Package

**Date**: 2026-04-20  
**Author**: Midgy BALON <midgy971@gmail.com>  
**Goal**: Add RK3568 support to the mainline `accel/rocket` driver (merged in 6.18 for RK3588).  
**Status**: Driver probes, IOMMU attaches, jobs submit — but PC DMA never starts (BUG A, see below).

---

## What This Is

The Rocket driver (`drivers/accel/rocket/`) was written by Tomeu Vizoso for the RK3588 NPU (3 cores,
6 TOPS). The RK3568 has the same NVDLA-derived IP with 1 core (0.8 TOPS). This port adds RK3568
support: a 5-patch series sitting at `patches/rocket-rk3568/`.

The RKNPU BSP driver cannot go upstream (tightly coupled to proprietary RKNN userspace). Rocket +
Teflon (Mesa delegate) is the correct mainline path.

---

## Hardware Summary (RK3568)

```
SoC:       Rockchip RK3568 (Cortex-A55 quad, 8GB LPDDR4X)
Board:     Radxa ROCK 3B
NPU IP:    NVDLA-derived, 1 core, 0.8 TOPS
IOMMU:     rockchip,iommu v1.0 at 0xfde4b000 — DTE must be sub-4GB (GFP_DMA32)
Clocks:    PVTPLL (via SCMI/TF-A) for 600–1000 MHz; CRU GPLL for ≤400 MHz
PMU:       NOC auto-idle must be disabled before NPU DMA
```

---

## What Works

| Feature | Status |
|---------|--------|
| Driver probe (`/dev/accel/accel0`) | ✅ |
| IOMMU attach + DTE sub-4GB (GFP_DMA32) | ✅ |
| IOMMU hardware enable (trigger_enable after NOC de-idle) | ✅ |
| Reset/retry cycle (per-job IOMMU detach+attach) | ✅ |
| Regcmd buffer content (sub-unit OP_EN writes in tail) | ✅ |
| No crashes / no kernel panic (as of Apr 19 kernel) | ✅ |
| PC DMA starting after OP_EN=1 | ❌ BUG A |
| Sub-unit execution + IRQ completion | ❌ (blocked by BUG A) |

---

## The Patch Series (`patches/rocket-rk3568/`)

| # | Subject | Notes |
|---|---------|-------|
| 0/5 | Cover letter | Explains RK3568 support, IOMMU dependency |
| 1/5 | `for_each_of_allnodes` → `of_find_matching_node` | Module can't use non-exported symbol |
| 2/5 | Add RK3568 SoC data | `rk3568_noc_init()` + `rk3568_soc_data` struct |
| 3/5 | DT binding | Add `rk3568-rknn-core`, `rockchip,pmu`, optional `sram-supply` |
| 4/5 | rk356x-base.dtsi | Rocket node, dual label, 200MHz default clock |
| 5/5 | rk3568-rock-3b.dts | `npu-supply`, drop `operating-points-v2` |

**Blocking submission**: inference test must pass first (BUG A). Also requires Simon Xue's IOMMU
per-device-ops patch (`<20260310105303.128859-1-xxm@rock-chips.com>`) for the GFP_DMA32 fix to
land cleanly upstream.

---

## BUG A: PC DMA Does Not Start

### Symptom
After writing `PC_OPERATION_ENABLE = 1`, `PC_INTERRUPT_RAW_STATUS = 0` at both 500µs and 500ms.
The PC hardware is not executing. Sub-units never receive OP_EN. IRQ never fires. Job times out.

### What IS confirmed correct
- IOMMU: DTE=0x044c8000 (sub-4GB ✓), STATUS=0x1 (paging enabled ✓)
- `PC_REGISTER_AMOUNTS = 0x84` = count−1 = 132 for 133 entries with scale=1 ✓
- `PC_TASK_CON = 0x6001` (TASK_PP_EN removed, only RESERVED_0+TASK_COUNT_CLEAR+TASK_NUMBER) ✓
- Regcmd tail verified: last 4 entries are sub-unit OP_EN writes:
  - `[0x00011008, 0x02010000]` → CNA OPERATION_ENABLE (CNA at NPU+0x1000)
  - `[0x00013008, 0x08010000]` → CORE OPERATION_ENABLE (CORE at NPU+0x3000)
  - `[0x00014008, 0x10010000]` and `[0x00015008, 0x20010000]` → DPU/PDP sub-units

### Register write sequence (current `rocket_job_hw_submit`)

```c
rocket_pc_writel(core, BASE_ADDRESS, 0x1);          // ← SUSPICIOUS: sets PC_SEL=1, addr=0
                                                     //   BSP does NOT do this write

rocket_cna_writel(core, S_POINTER, PP_EN|...);      // CNA ping-pong pointer setup
rocket_core_writel(core, S_POINTER, PP_EN|...);     // CORE ping-pong pointer setup

rocket_pc_writel(core, BASE_ADDRESS, task->regcmd); // actual regcmd IOVA
rocket_pc_writel(core, REGISTER_AMOUNTS, count-1);
rocket_pc_writel(core, INTERRUPT_MASK, DPU_0|DPU_1);
rocket_pc_writel(core, INTERRUPT_CLEAR, DPU_0|DPU_1);
rocket_pc_writel(core, TASK_CON, 0x6001);
rocket_pc_writel(core, TASK_DMA_BASE_ADDR, 0);
rocket_pc_writel(core, OPERATION_ENABLE, 1);
```

**BSP sequence** (only 5 writes, no BASE_ADDRESS=0x1):
```
DATA_ADDR (0x10)   = regcmd_iova
DATA_AMOUNT (0x14) = (count + 4 + scale-1) / scale - 1   ← EXTRA_AMOUNT=4
TASK_CONTROL (0x30) = 0x6001
DMA_BASE_ADDR (0x34) = task_base_addr
OP_EN (0x08) = 1
```

### Top hypotheses (updated after comparing with upstream RK3588 driver)

**H1 (MOST LIKELY): `pc_data_amount_scale=1` is wrong — should be scale=2**  
Upstream RK3588 driver uses `(count + 1) / 2 - 1` (= scale=2). Our `rk3568_soc_data` uses scale=1.

For count=133:
- scale=1 → AMOUNTS=132 (what we write)
- scale=2 → AMOUNTS=66  (what RK3588 writes)

If RK3568 PC hardware also counts in 128-bit pairs (same IP), AMOUNTS=132 tells it to read
133 pairs = 2128 bytes, but the buffer is only 1064 bytes. PC either faults on unmapped
pages beyond the buffer, or interprets the count differently — in either case, it may abort
before starting DMA, leaving RAW_STATUS=0.  
**Fix**: Change `pc_data_amount_scale` to 2 in `rk3568_soc_data` in `rocket_drv.c`.

**H2: `TASK_PP_EN` is still functionally set via another mechanism**  
Upstream includes `PC_TASK_CON_TASK_PP_EN(1)` for RK3588 and inference works. We removed it
for RK3568. If the bit has a different meaning on RK3568 (bit 12 vs bit 12 on RK3588), or if
the BSP `0x6 << 12 = 0x6000` means bits 13+14 and NOT bit 12 as we assumed, there may be
a different register field at play.  
**Note**: `BASE_ADDRESS=0x1` and `INTERRUPT_MASK=DPU_0|DPU_1` are IDENTICAL to upstream and
work on RK3588 — these are confirmed NOT the cause of BUG A.

**H3: INTERRUPT_MASK semantics — confirmed 1=ENABLE**  
Same write exists in upstream RK3588 driver (which works). Upstream writes MASK=0 in the IRQ
handler (standard mask-on-entry pattern). So 1=enable. NOT the issue.

---

## Key Driver Changes vs Upstream (RK3588 Rocket)

### `rocket_job.c`
- `rocket_job_run`: detach-before-attach + `attached_domain` tracking to avoid EBUSY
- `rocket_job_run`: `rk_iommu_trigger_enable(core->iommu_dev)` after `iommu_attach_group` (deferred HW enable)
- `rocket_job_hw_submit`: `TASK_CON = 0x6001` (TASK_PP_EN removed — see insight.md)
- `rocket_job_hw_submit`: no OP_EN pulse (write 1 only — see insight.md)
- `rocket_job_hw_submit`: REGCMD tail dump + POST500US=PC_RS only (no CNA/CORE reads)
- `rocket_job_timedout`: no NPU register reads (AXI stall safety)
- `rocket_job_handle_irq`: no `iommu_detach_group(NULL)` on completion

### `rocket_drv.c`
- `rocket_device_probe`: PVTPLL two-step rate change (600MHz→1GHz); explicit PMU power-on
- `rocket_device_probe`: `iommu_dev` lookup via `of_parse_phandle("iommus")`
- `rocket_device_runtime_suspend`: `rk_iommu_trigger_disable()` before clock gate
- `rocket_device_runtime_resume`: detach stale IOMMU domain on resume

### `rocket_core.h`
- Added `iommu_dev`, `attached_domain` fields

### `drivers/iommu/rockchip-iommu.c` (NOT in the patch series — separate fix)
- Added `hw_needs_enable` guard in `rk_iommu_attach_device` (~line 1086)
- Added `rk_iommu_trigger_enable()` / `rk_iommu_trigger_disable()` exported functions
- GFP_DMA32 for iommu_data_ops_v2 page table allocations

---

## Known NPU AXI Bus Rules (CRITICAL)

These rules were learned through hardware crashes — violating them causes hard lockups:

1. **Never read CNA/CORE MMIO while sub-units are executing DMA**  
   AXI bus has no timeout for MMIO reads. CPU `readl` to CNA/CORE stalls indefinitely if those
   sub-units are mid-DMA. Result: hard lockup, requires power cycle.

2. **Never add the BSP OP_EN pulse (write 1 then 0) to Rocket driver**  
   BSP's `REG_WRITE` has no DSB barrier. Rocket's `writel` includes ARM64 DSB. With barriers,
   both writes complete before PC hardware samples OP_EN — hardware sees final value 0 and never
   starts. Use single `writel(1)` only.

3. **Never read PC MMIO while PC is actively broadcasting regcmd**  
   PC reads are safe while PC is idle (after broadcasting). Not during broadcast phase.

4. **In timeout handler, skip ALL NPU register reads**  
   At timeout, PC/CNA may be in DMA fault state with the AXI bus stalled. Any `readl` to NPU
   MMIO hard-lockups the CPU.

---

## Bugs Fixed During This Port

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| NPU bus never de-idles | PVTPLL must be running before PMU de-idle ACK | Two-step `clk_set_rate` in `rknpu_power_on` |
| IOMMU PTE=0 (DTE above 4GB) | `gfp_flags=0` on 8GB board → DTE above 4GB | `iommu_data_ops_v2.gfp_flags = GFP_DMA32` |
| IOMMU attach crashes (hard lockup) | `DL_FLAG_PM_RUNTIME` link triggers `rk_iommu_enable` on already-CRU-reset IOMMU | `hw_needs_enable` guard in `rk_iommu_attach_device` |
| TASK_PP_EN bug: PC never starts | Bit 12 in TASK_CON enabled ping-pong mode — PC waited forever for second buffer | Remove `PC_TASK_CON_TASK_PP_EN(1)` |
| EBUSY loop after timeout | `rk_iommu_suspend` reset `iommu->domain` before detach could run | detach-before-attach + `attached_domain` tracking |
| POST500US crash | CNA AXI read while CNA executing → stall | Remove CNA/CORE reads; keep only PC_RAW_STATUS |

---

## File Index

```
for_review/
├── REVIEW.md                    ← this file
├── CLAUDE.md                    ← project status, build commands, boot config
├── insight.md                   ← hardware-specific technical insights (read first!)
├── driver/
│   ├── rocket/                  ← complete modified accel/rocket driver
│   │   ├── rocket_job.c         ← most modified: IOMMU flow, submit, reset
│   │   ├── rocket_drv.c         ← probe, power, PVTPLL, iommu_dev lookup
│   │   ├── rocket_core.c/.h     ← added iommu_dev, attached_domain fields
│   │   ├── rocket_registers.h   ← unmodified (reference)
│   │   └── ...
│   └── iommu/
│       └── rockchip-iommu.c     ← hw_needs_enable guard + trigger_enable/disable
└── patches/
    └── rocket-rk3568/           ← 5-patch series (0000-cover through 0005)
```

---

## Questions for Reviewer

1. **`pc_data_amount_scale` for RK3568**: Upstream RK3588 hardcodes `(count+1)/2-1` (scale=2).
   Our `rk3568_soc_data` uses scale=1. Does the RK3568 NVDLA PC count AMOUNTS in 64-bit or
   128-bit units? The TRM Part1 section on the PC DMA REGISTER_AMOUNTS field would clarify this.
   This is the most likely cause of BUG A — `pc_data_amount_scale=2` should be tested.

2. **`TASK_PP_EN` (bit 12) on RK3568**: Upstream (RK3588) sets this and inference works. BSP
   (RK3568) writes `0x6001 = (0x6 << 12) | 1` which we decode as bits {0,13,14}. But if
   `pc_task_number_bits=12` means the task count field occupies bits [11:0], then bits [13:12]
   are a 2-bit mode field, not individual flags. `0x6 << 12 = 0x6000` sets both bits 13 and 14.
   BSP effectively sets TASK_PP_EN=0 and TASK_COUNT_CLEAR=1. We agree on this, but can you
   confirm from TRM or RTL that bit 12 is ping-pong and that RK3568 PC with single-task mode
   should NOT have it set?

3. **`rockchip-iommu.c` changes**: The `hw_needs_enable` guard and `trigger_enable/disable`
   exports are RK3568-specific workarounds for the deferred IOMMU enable race with
   `DL_FLAG_PM_RUNTIME` device links. Is there a cleaner way to handle this without exporting
   functions from the IOMMU driver? Could `iommu_attach_group` be called later (after NOC
   de-idle) instead of requiring a deferred enable?

4. **NOC de-idle per-job**: `rk3568_noc_init()` is called only in `rocket_device_runtime_resume`
   and `rocket_core_reset`. If the NPU NOC auto-idles between the runtime_resume and the actual
   job submission (PM autosuspend delay is non-zero), does the PC DMA bus need to be de-idled
   again at job submit time?
