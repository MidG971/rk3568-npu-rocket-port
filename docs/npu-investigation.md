# NPU Power Domain Investigation History

Detailed hardware investigation sessions for the RK3568 NPU power domain issue on mainline kernel.

## Root Cause Summary

**The NPU bus de-idle ONLY works when TF-A `clk_npu_set_rate()` is called via SCMI.**

- CRU-only `assigned-clocks` does NOT trigger de-idle
- SCMI writes additional CLKSEL_CON7 bits (6, 7, 8, 15) that initialize PVTPLL
- PVTPLL must be running for the NPU bus to respond to PMU de-idle requests
- DTS must use `assigned-clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>`

BSP kernel works because the BSP bootloader initializes the NPU NoC interconnect;
mainline U-Boot does not replicate this initialization.

---

## Session 2026-02-05: Initial Hardware Investigation

### PMU Register Architecture Discovery

ARM Trusted Firmware source at `~/uboot-rock3b/arm-trusted-firmware/plat/rockchip/rk3568/`:

PMU has two sets of registers (from `pmu.h`):
- Hardware auto-idle: `PMU_BUS_IDLE_CON0` (0x040)
- Software idle: `PMU_BUS_IDLE_SFTCON0` (0x050) ← kernel uses this
- Hardware power gate: `PMU_PWR_GATE_CON` (0x090)
- Software power gate: `PMU_PWR_GATE_SFTCON` (0x0a0) ← kernel uses this
- Voltage gate: `PMU_VOL_GATE_SFTCON` (0x0a8) with `VD_NPU_ENA` (bit1), `VD_GPU_ENA` (bit0)

TF-A SCMI: `clk_scmi_npu_set_status()` is a **NO-OP** — returns 0 without doing anything.
Only `clk_npu_set_rate()` (rate-setting) does real work.

### All Direct PMU Approaches Failed

1. ✗ Ungating ALL NPU CRU clocks — de-idle still fails
2. ✗ Disabling NOC_AUTO_ENA for NPU — de-idle still fails
3. ✗ Clearing VD_NPU voltage gate — de-idle still fails
4. ✗ Full power cycle (OFF→ON) via PMU registers — de-idle still fails
5. ✗ Clearing HW BUS_IDLE_CON0 in addition to SFTCON — de-idle still fails
6. ✗ Complete sequence (clocks + voltage + power cycle + resets + de-idle) — STILL FAILS

### GPU vs NPU Comparison

GPU de-idle: **SUCCESS** (GPU_ID readable: 0x74021000)
NPU de-idle: **FAILED** (ACK=0x1ee, ST=0x1ee — NPU bit2=1 = still idle)

Both use identical PMU idle protocol. Something else is different.

### One-Time NPU Activity (Clue)

During one test, NPU briefly came active:
```
[300ms] ACK=0x000001ec ST=0x000001ec  ← NPU bit2=0 (ACTIVE)
[350ms] ACK=0x000001ec ST=0x000001ec  ← Still active
[450ms] ACK=0x000001ee ST=0x000001ee  ← Back to idle
```
This proved the NPU CAN de-idle, but something else was keeping it idle.

---

## Session 2026-02-05: BSP Kernel Test — Root Cause Found

### BSP Works Perfectly

Booted BSP kernel 5.10.160-18-rk356x. NPU worked:
```
Before NPU access:  PWR_STATUS=0x7f (NPU=OFF), BUS_IDLE=0x1ee (IDLE)
After ioctl:        PWR_STATUS=0x7d (NPU=ON),  BUS_IDLE=0x1ea (ACTIVE) ✓
NPU registers readable without crash!
```

### BSP DT Difference: VDD_NPU Regulator

BSP has `regulator-always-on` + `regulator-boot-on` on VDD_NPU. Mainline did not.

**But this was not the root cause.** After adding VDD_NPU always-on to mainline:
- VDD_NPU: ✅ enabled at 500mV
- All clocks: ✅ ungated
- Power domain: ✅ ON
- De-idle: ❌ STILL FAILS

### Confirmed Root Cause: Bootloader Initialization

| | BSP Bootloader | Mainline U-Boot |
|---|---|---|
| After boot, before NPU access | BUS_IDLE=0x1ee (IDLE) | BUS_IDLE=0x1ee (IDLE) |
| After ioctl triggers genpd | BUS_IDLE=0x1ea (ACTIVE) ✅ | BUS_IDLE=0x1ee (IDLE) ❌ |

Same kernel code, same hardware, different bootloader = different result.

---

## Session 2026-02-06: Write-Mask Protocol Discovery

### Critical Bug Found

PMU registers use write-mask semantics:
- Upper 16 bits = write enable mask
- Lower 16 bits = value

Previous driver code was doing READ-MODIFY-WRITE on write-mask registers:
```c
// WRONG:
val = readl(pmu + 0x70);
writel(val & ~0x4, pmu + 0x70);  // writes 0x0000fffb → mask=0, IGNORED!

// CORRECT:
writel((1 << 18) | 0, pmu + 0x70);  // mask=bit2, value=0 → clears bit2
```

Verified by test: `Write 0x00040000 (write-mask) → Read back: 0x0000fffb` ✓

Even after fixing this, NPU still didn't de-idle. The write-mask fix was necessary
but not sufficient.

---

## Session 2026-02-06: U-Boot Investigation

### Mainline U-Boot Analyzed

Compared BSP vs mainline `arch/arm/mach-rockchip/rk3568.c`:
- BSP has `qos_priority_init()` which powers on domains and de-idles buses
- Mainline only enables VO (display) domain, no bus init

Added `qos_priority_init()` to mainline U-Boot — partial effect but NPU still fails.

### Paradox Finding

BSP has MORE restrictive settings (NOC auto-idle enabled, voltage gated) but NPU works.
Mainline has LESS restrictive settings (auto-idle disabled, voltage cleared) but NPU fails.

Simple register-level patches to mainline U-Boot cannot replicate BSP behavior.
The BSP U-Boot must have fundamentally different early initialization.

---

## Session 2026-02-13: SCMI Root Cause Discovery

### The Real Fix

The NPU requires SCMI clock rate call to initialize PVTPLL:

1. `clk_set_rate(scmi_clk_npu, 600MHz)` → TF-A `clk_npu_set_rate()` runs
2. TF-A writes CLKSEL_CON7 bits 6, 7, 8, 15 → initializes PVTPLL ring oscillator
3. With PVTPLL running, NPU bus CAN respond to PMU de-idle
4. PMU de-idle (write-mask to BUS_IDLE_SFTCON0) succeeds

**Kernel clock framework optimization problem**: If `clk_set_rate(rate)` is called with the same rate already set, the framework skips the call (no-op). Fix: call two different rates (600MHz then target) to force TF-A execution.

### TF-A SCMI Bug Fixed

`clk_scmi_npu_get_rate()` returned 0 when PVTPLL mode was active (bit 15 of CLKSEL_CON7).
Fixed to return `sys_clk_info.npu_rate` when bit 15 is set.

File: `arm-trusted-firmware/plat/rockchip/rk3568/drivers/scmi/rk3568_clk.c`

---

## Session 2026-02-06: genpd always_on Fix

### Problem with Standard genpd

With `power-domains = <&power RK3568_PD_NPU>`:
1. pm-domains tries to power on NPU → fails de-idle
2. RKNPU driver probes anyway (non-iommu mode)
3. At end of probe, `rknpu_power_off()` called
4. genpd tries to save QoS registers → NPU not accessible → crash:

```
rockchip_pd_power_off+0x1c/0x60
genpd_runtime_suspend+0x130/0x298
rknpu_power_off+0x20/0xa8
rknpu_probe+0x504/0x750
Internal error: synchronous external abort: 96000010
```

### Solution: always_on Domain

Added `DOMAIN_RK3568_AO` macro to `drivers/pmdomain/rockchip/pm-domains.c`:
- `always_on = true` field in domain info
- `rockchip_pd_power_on/off`: return 0 immediately for always_on domains
- `pm_genpd_init`: force `is_off = false` for always_on domains
- `[RK3568_PD_NPU] = DOMAIN_RK3568_AO("npu", BIT(1), BIT(2), false)`

This allows the driver to use `power-domains` binding correctly without the
genpd framework trying to power-cycle the NPU (which it can't do correctly
without the SCMI+PVTPLL initialization sequence).

---

## PMU Register Quick Reference

| Register | Offset | NPU Bit | Type | Purpose |
|----------|--------|---------|------|---------|
| BUS_IDLE_CON0 | 0x40 | bit2 | Config | HW auto-idle enable |
| BUS_IDLE_SFTCON0 | 0x50 | bit2 | Write-mask | SW idle request |
| BUS_IDLE_ACK | 0x60 | bit2 | Read-only | Idle acknowledge |
| BUS_IDLE_ST | 0x68 | bit2 | Read-only | Idle status |
| NOC_AUTO_CON0 | 0x70 | bit2 | Write-mask | NOC auto-idle |
| PWR_DWN_ST | 0x98 | bit1 | Read-only | Power status (0=ON) |
| PWR_GATE_SFTCON | 0xa0 | bit1 | Write-mask | Power gate |
| VOL_GATE_SFTCON | 0xa8 | bit1 | Write-mask | Voltage gate |

Write-mask format: `(1 << (bit + 16)) | (value << bit)`

PMU base address: `0xfdd90000` (RK3568)

---

---

## Session 2026-03-27/29: IOMMU Mode Debugging

### Context

IOMMU mode was probing correctly as of 2026-03-26. Jobs were failing with `task counter: 0x0, irq_status: 0x0, raw_status: 0x10000`. GIC SPI 151 (Linux IRQ 183) showed zero count throughout the 6s timeout — no interrupt reached GIC despite NPU INT_RAW_STATUS=0x10000 (bit 16 set).

### Discovery: RK3568 IOMMU fault is NOT independently wired to GIC

On RK3568, the IOMMU (at 0xfde4b000) shares GIC SPI 151 with the NPU. An IOMMU fault does NOT directly assert a GIC line — instead it asserts **bit 16 of the NPU's INT_RAW_STATUS** register (at 0xfde40000+offset). The interrupt only reaches GIC if bit 16 is included in the NPU's INT_MASK register.

RKNN userspace sets `int_mask = 0x300` (bits 8,9 = job completion). Bit 16 was excluded → IOMMU faults were masked at the NPU interrupt controller → GIC count stayed 0 → 6s timeout.

**Fix applied to rknpu_job.c:**
1. Write `INT_MASK = last_task->int_mask | (iommu_en ? BIT(16) : 0)` before OP_EN
2. Handle BIT(16) in rknpu_irq_handler: clear INT_CLEAR, call rknpu_job_done(-EFAULT)

### First IOMMU execution observed

With bit-16 handler in place, `rk_iommu_irq` began firing. One run completed in **8.80ms** (real NPU execution, not timeout). However all inference outputs were 0.000000 — confirming the PTE=0 bug (NPU DMAing through IOVA→PA=0 translations, reading/writing zeroed pages).

### PTE=0 bug analysis

`log_iova` diagnostic (from rk_iommu_irq) showed:
```
dte@0xe1000ff4: 0xdf670001 valid: 1
pte@0xdf670fc0: 0x000000 valid: 0
```
DTE is valid (pt_address = 0xdf670000), but PTE[1008] at phys 0xdf670fc0 reads 0 from both hardware AND CPU (`phys_to_virt`).

Initial hypothesis: SWIOTLB bounce buffer issue with `dma_bit_mask = DMA_BIT_MASK(32)`. Theory: PTE pages allocated above 4GB → SWIOTLB → `dma_sync_single_for_device(DMA_TO_DEVICE)` overwrites PTE writes with zeros from original page.

**This hypothesis was wrong**: the board has 4GB RAM. All physical addresses are < 4GB. DMA_BIT_MASK(32) is sufficient — no SWIOTLB is triggered.

### Regression: dma_bit_mask change caused BUS_ERROR

Changed `iommu_data_ops_v2.dma_bit_mask` from `DMA_BIT_MASK(32)` → `DMA_BIT_MASK(40)`. After rebuild and reboot, new failure mode:

- IOMMU fires within 100us of OP_EN
- `rk_iommu_irq` reads INT_STATUS = **BUS_ERROR (bit 1)**, not PAGE_FAULT (bit 0)
- "BUS_ERROR at 0x0000000000000000" — PAGE_FAULT_ADDR register is 0 (not set for bus errors, misleading)
- `rknpu_irq_handler` bit-16 handler does NOT fire → still 6s timeout

**BUS_ERROR** = IOMMU got AXI error response when trying to read a DTE or PTE entry from DRAM. Not a translation miss.

### Mystery: why does rknpu_irq_handler not see bit 16 after rk_iommu_irq?

Both handlers share GIC SPI 151 (IRQF_SHARED). Linux calls all handlers in sequence:
1. `rk_iommu_irq` runs → reads IOMMU INT_STATUS → writes `RK_MMU_INT_CLEAR` → returns IRQ_HANDLED
2. `rknpu_irq_handler` runs → reads NPU INT_STATUS → should see bit 16 → but doesn't (6s timeout)

Hypothesis: NPU INT_RAW_STATUS bit 16 is a **level-triggered signal** fed from the IOMMU interrupt line. When `rk_iommu_irq` writes IOMMU INT_CLEAR, it de-asserts the IOMMU interrupt line → NPU bit 16 also goes LOW before `rknpu_irq_handler` reads INT_STATUS.

**Fix needed**: Read `INT_RAW_STATUS` instead of `INT_STATUS` in rknpu_irq_handler for bit-16 detection — RAW_STATUS is not gated by the IOMMU INT_CLEAR and may persist.

### rk_iommu_enable clock model

`rk_iommu_enable` always ends with `clk_bulk_disable` (IOMMU register-access clocks off). This is intentional: the IOMMU page-walk clocks are the **same hardware clocks** as the NPU's ACLK_RKNPU/HCLK_RKNPU. When the NPU driver calls `clk_bulk_prepare_enable` for its own clocks, it also holds a reference to these clocks. The clock framework only gates them off when ALL references are released. So IOMMU page-walk clocks remain active as long as the NPU is powered.

### Outstanding questions

1. Why did the dma_bit_mask change cause BUS_ERROR when the board has 4GB RAM?
   - Possibility: changing coherent_dma_mask from 32→40 affects DMA mapping behavior in a subtle way
   - Action: revert to DMA_BIT_MASK(32) and confirm BUS_ERROR goes away

2. With BUS_ERROR gone (after revert): does PTE=0 persist?
   - If yes: add debug print inside rk_iommu_map_iova to verify the write and readback
   - Also check: rknpu_gem.c line ~261 overwrites sg_dma_address with sg_phys — this may corrupt the IOVA stored in the gem object, causing wrong address to be passed to job_commit

3. Does the rknpu_irq_handler need to read RAW_STATUS for bit-16 detection?
   - If so: change `status = REG_READ(RKNPU_OFFSET_INT_STATUS)` to read RAW_STATUS for the bit-16 check

---

## Session 2026-03-29 (continued): Domain Stability + iova_to_phys=0 Root Cause Hunt

### Domain pointer confirmed stable

Added domain tracking debug prints to `rockchip-iommu.c`:
- `rk_iommu_identity_attach`: prints when domain is detached
- `rk_iommu_attach_device`: prints when domain is attached
- `rk_iommu_enable`: prints domain pointer + DTE_ADDR at enable time
- `rk_iommu_resume`: prints domain pointer before re-enable

Test run showed **no attach/detach prints after `dmesg -c`** — domain does NOT change between probe and inference. Previous session's DTE_ADDR mismatch (0xe2000000 vs 0xe1000000) was due to reading different boots, not runtime domain switching.

### iova_to_phys=0 confirmed (new gem debug print)

Added to `rknpu_gem.c` `rknpu_gem_get_pages()`:
```
gem_get_pages: dma_addr=0xff7f0000 iova_to_phys=0 size=...
```
`iommu_iova_to_phys(domain, 0xff7f0000)` returns 0 immediately after `dma_map_sg` returns. This is the root symptom: the IOVA allocated by `dma_map_sg` is not registered in the domain.

### Code path confirmed: iova_aligned=true → dma_map_sg

`rknpu_gem_get_pages` uses `rknpu_iommu_dma_map_sg` with `iova_aligned=true` (flags=0 for test buffers), which calls `dma_map_sg(dev, sg, nents, dir)` → `iommu_dma_map_sg` → `rk_iommu_map`.

### rk_map print was ratelimited

The `rk_map:` print in `rk_iommu_map` used `pr_info_ratelimited`. Boot-time IOMMU mappings (DRM framebuffer, USB, etc.) consumed the ratelimit bucket → NPU-time calls were silently suppressed.

**Fix applied**: Changed to unconditional `pr_warn` with domain pointer. Also added `pr_warn` after `rk_table_flush` in `rk_iommu_map_iova` to verify PTE[0] value post-flush.

### IOVA 0xff7f0000 analysis

`dte_index = 0x3fd`, `pte_index = 0x3f0` → near the top of 4GB address space. Two possibilities:
1. This is a real IOVA from the DMA IOVA allocator (upper end of IOVA space)
2. This is actually a physical address being passed directly (bypassing IOMMU)

The fact that `DTE[0x3fd]` is valid (DTE was written) but `PTE[0x3f0]=0` after flush is the exact pattern of `rk_iommu_map` being called but the PTE write not persisting.

### Next steps

1. Rebuild with unconditional `rk_map:` prints on cross-compile server (192.168.1.117)
2. Run test with serial log — confirm whether `rk_iommu_map` is called for IOVA=0xff7f0000
3. If called but PTE=0 after flush → cache coherency issue in `rk_table_flush`
4. If NOT called → `dma_map_sg` is not calling `rk_iommu_map` (domain mismatch at DMA layer?)
5. Revert `dma_bit_mask` to `DMA_BIT_MASK(32)` if BUS_ERROR persists after rk_map fix

---

## Possible Future Paths for Upstream NPU Support

1. **Fix mainline rockchip-pm-domains** for RK3568 NPU — proper power-on sequence
2. **Add RK3568 support to Rocket driver** (`accel/rocket`) — correct upstream path
   - Rocket merged in 6.18 for RK3588
   - RK3568 NPU = same NVDLA-derived IP (1 core 0.8 TOPS vs 3 cores 6 TOPS)
3. **BSP RKNPU cannot be submitted upstream** — requires proprietary RKNN SDK userspace
