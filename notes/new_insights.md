# RK3568 NPU Compute Gate — Diagnostic Insights & Hypotheses

This document outlines the current state of the mainline **`rocket`** accel driver bring-up on the Radxa ROCK 3B (RK3568 SoC, 8GB RAM), specifically focusing on the unresolved **NPU Compute Gate** (the "executer-engage" stall). It synthesizes historical findings, community/vendor feedback, and recent source-code analyses to formulate actionable hypotheses and concrete test protocols.

---

## 1. Executive Summary & Project Status

### The Bring-Up Victory (Mainline v7.1-rc6)
As of **June 10, 2026**, the mainline kernel integration and device tree architecture have been beautifully solved. The upcoming **RFC v4** changeset cleanly resolves the platform bring-up:
- **PMDomain Deadlock Resolved (`CONFIG_DRM_ACCEL_ROCKET=m`)**: The RCU stall/freeze on boot (caused by the NPU power domain's child attempting to communicate with the I2C-based RK809 PMIC before the scheduler/RCU was fully initialized) has been bypassed. By building the driver as a module (`=m`), rocket probes safely at `~6.84s` post-boot, loading the PM domain cleanly without any "always-on" hacks.
- **Simon Xue's Per-Device IOMMU Ops**: Successfully integrated, allowing the NPU's v2 MMU to operate under a 32-bit DTE cap while keeping the other SoC IOMMUs at a 40-bit limit.
- **AUTO_GATING Patch Dropped**: The AUTO_GATING bit-31 workaround (`DISABLE_FETCH_DTE_TIME_LIMIT`) has been merged unconditionally in `iommu/next` (slated for v7.2), so our series no longer needs to carry any IOMMU driver modifications.
- **CLK_NPU Assigned Clock Fixed**: The NPU AXI bus clock (`aclk_npu`) has been successfully assigned via DT to run at `200 MHz` (up from the `12 MHz` boot default), matching the BSP.

### The Last Blocker: The NPU Compute Gate
While the driver successfully probes, attaches the IOMMU, and submits jobs, **the actual convolution never completes**. 
- **Symptoms**: All 5 sub-units (CNA, CMAC, ACCU, DPU, DPU_RDMA) remain stuck at `S_STATUS = 0x05` (ping-pong loaded, "executer operating") for the entire `~540 ms` timeout.
- **The Litmus**: The DMA read-bandwidth counters show that CNA reads the entire input tensor (`dt_rd ≈ 0x328d`) and weight tensor (`wt_rd = 0x1900`), but the DPU output-write counter remains exactly `dt_wr = 0`. The hardware-set `S_POINTER` bit 16 (0x10000, "executer engaged") never sets.
- **Rule-Outs**: 
  - **Command stream content** is NOT the gate. Replaying the vendor’s byte-exact 133-entry `regcmd` still stalls at `dt_wr = 0`, whereas the same stream on the BSP produces the complete output.
  - **Submit-time MMIO writes** are NOT the gate. Tracing and matching the BSP's exact 12 MMIO submissions (including `TASK_CON = 0x7001` and the single `OP_EN` pulse) yields the same failure.
  - **Cache coherency** is NOT the gate. Inputs and weights are verified in physical DRAM.

---

## 2. Core Hypotheses for the Compute Gate

The bug is isolated to the **CNA (data-loaded) → CBUF → CMAC (compute)** hand-off. The hardware successfully loads inputs and weights into the CBUF, but the MAC array never fires to consume them, resulting in a pipeline deadlock. 

Below are the four most likely hypotheses for why the executer fails to engage.

### Hypothesis A: The Silent PVTPLL (Compute-Clock is Off)
*The DMA/bus engines are clocked and read data, but the MAC computation engines have no clock, causing an immediate stall.*

- **The Mechanism**: The NPU SoC bus interface (responsible for DMA reads and register access) is clocked by `aclk_npu` / `hclk_npu` from the CRU, which are verified to be running at `200 MHz`. However, the core MAC/matrix execution array is clocked by the **PVTPLL** (the NPU ring oscillator). The PVTPLL is started via an SCMI set-rate call to TF-A.
- **Why it is likely**: 
  1. For ~2 years, rocket's clock framework mapped `clks[2]` (the SCMI compute clock) incorrectly to ACLK due to an unset clock ID. Although this is fixed in the current v4 changeset via `of_clk_get_by_name("npu")`, it is highly possible that the mainline clock framework or the SCMI driver is bypassing the clock transition (e.g., due to 'unchanged rate' cached optimizations in the kernel or SCMI agent), leaving PVTPLL uninitialized or turned off in TF-A.
  2. The BSP explicitly uses a two-step rate change `clk_set_rate(600MHz)` -> `clk_set_rate(1GHz)` to force TF-A to rebuild/re-enable the PVTPLL.
- **How to Test**:
  1. Boot the board into the rocket test entry (`l3`/`l5`/`l6`).
  2. Run `teflon_test.py` in a loop and query:
     ```bash
     cat /sys/kernel/debug/clk/clk_summary | grep -i npu
     ```
  3. Verify if `clk_npu_pvtpll` (or the SCMI clock node) actually transitions to `1 GHz` during execution.
  4. Compare this exact clock summary tree against a working BSP boot (`l1` entry) running `conv2d-bsp.rknn`. If PVTPLL shows as `0 MHz` or is bypassed on mainline, the compute array has no clock.

---

### Hypothesis B: Incomplete Reset-Release and PVTPLL Sync
*The hardware reset sequence used by rocket is too minimal, leaving internal pipeline registers in an un-armed state.*

- **The Mechanism**: In `rknpu_reset.c`, the vendor driver notes that a CRU soft reset resets the PVTPLL ring oscillator. After deasserting resets, the vendor forces the PVTPLL SCMI set-rate *before* attempting IOMMU detach/reattach or PMU de-idle.
- **Why it is likely**: Rocket's default reset sequence (`rocket_core_reset`) only asserts and deasserts `srst_a` and `srst_h` via the bulk reset framework. Even when `full_reset = 1` (the ported vendor-reset sequence) is used, if the exact timing (such as the `msleep(100)` settle time, or the precise timing of the SCMI clock rate-change immediately post-deassertion) is slightly out of phase with the PMU de-idle, the executer block's state machine may fail to clear its internal halt bits.
- **How to Test**:
  - Review `rocket_full_reset()` in `rocket_core.c` and compare the exact timing with `rknpu_soft_reset()` in `srcdiff/rknpu_reset.c`.
  - Ensure that:
    1. Resets are asserted.
    2. Resets are deasserted.
    3. **Immediately** afterwards, the two-step `dev_pm_opp_set_rate(core->dev, 600M)` then `(1G)` is fired.
    4. **Only after** the clocks are running, `noc_init()` (PMU de-idle) is executed.
    5. **Only after** de-idle, `rk_iommu_trigger_enable()` is called.
  - If any of these steps are run out of order in rocket's runtime resume path compared to the BSP's power-on/reset path, the execution state machine will not arm.

---

### Hypothesis C: Omitted GRF/PMU Internal Clock-Gating Configuration
*Internal NPU hardware clock-gates are active, blocking the SCMI clock from reaching the CMAC sub-units.*

- **The Mechanism**: Many Rockchip SoCs feature high-level power/clock gating controllers inside the GRF (General Register Files) or PMU. These registers gate clocks to individual IP sub-blocks (like the CMAC/ACCU/DPU) even if the top-level IP clock (`clk_npu`) is enabled.
- **Why it is likely**: In `rknpu_drv.c`, we see direct PMU writes to `0xfdd90000` (disabling NOC auto-idle, clearing power-gate bits, etc.). There might be undocumented GRF registers (e.g., `GRF_SOC_CON*`) or PMU registers that the vendor's board setup (or early bootloader / TF-A phases) configures, but which are left unconfigured under mainline.
- **How to Test**:
  - Boot the board on the working BSP (`l1`) and dump the PMU and NPU-related GRF registers.
  - Boot the board on mainline (`l3`) and dump the same registers.
  - Specifically, inspect:
    - PMU registers around `0xfdd90000` (NOC_AUTO_CON0, PWR_GATE_SFTCON, etc.)
    - GRF registers around `0xfdc60000` (or the relevant GRF address range for RK3568 NPU)
  - Verify if any clock-gating or power-routing bits differ between the two boots.

---

### Hypothesis D: AXI Write-Channel Protection or Secure Firewall Gating
*The NPU compute array executes, but its output AXI write transactions are silently dropped/blocked by a security firewall or bus-level filter.*

- **The Mechanism**: Under some security configurations, the NPU might be allowed to perform AXI read transactions but is blocked from issuing AXI writes to system DRAM, or the DPU write target is filtered out by the bus.
- **Why it is likely**:
  - The DMA read-bandwidth counters (`dt_rd`, `wt_rd`) increment, proving the NPU has bus-master read privileges.
  - The DMA write counter (`dt_wr`) is strictly 0.
  - No IOMMU page faults are reported, and the IOMMU status returns to idle (`ST = 0x19`). If a write transaction was blocked by a standard IOMMU page fault, we would see an interrupt. However, if the write was blocked at the AXI bus firewall level (such as Rockchip's internal master-firewalls) or a TrustZone protection zone, the transaction would be silently aborted or lead to a bus freeze.
- **How to Test**:
  - Run the `teflon_test.py` on rocket using the **non-IOMMU** boot entry (`l2` fallback). This bypasses the IOMMU entirely and uses physical address mapping.
  - If non-IOMMU mode successfully writes output or results in a bus hang, the issue is directly tied to IOMMU domain/DTE write-permissions or secure-world configurations.

---

## 3. High-Leverage Verification Steps

To resolve this gate efficiently, the following three-step diagnostic plan is recommended:

### Step 1: Side-by-Side SCMI Clock Summary Comparison
Run the convolution on both boot entries and capture the clock trees:
```bash
# On l1 boot (Working BSP):
cat /sys/kernel/debug/clk/clk_summary | grep -i npu > bsp_clks.txt

# On l6 boot (Mainline rocket):
cat /sys/kernel/debug/clk/clk_summary | grep -i npu > rocket_clks.txt

diff -u bsp_clks.txt rocket_clks.txt
```
*Goal*: Confirm if `clk_npu_pvtpll` is indeed running at `1 GHz` on mainline during the job.

### Step 2: Instrument and Dump All MMIO Writes during BSP Submit
To rule out any undocumented register writes performed by the vendor driver during job submission:
1. Instrument `rknpu_job.c` or `rknpu_drv.c` on the BSP kernel to log every single `writel` or `regmap_write` to the NPU's address space (`0xfde40000` to `0xfde4ffff`) during a single `conv2d` run.
2. Compare this MMIO trace with rocket's MMIO trace during a job submit.
3. If there is even a single write to a control register (e.g., `0x1004` or global control space) that rocket is missing, replicate it.

### Step 3: Engage Silicon and Community Experts
Since the bring-up layers are clean and RFC v4 is prepared, share these highly-targeted findings with the community:
1. **Chaoyi Chen (Rockchip)**: Present the detailed `S_POINTER` bit-16 findings. Since he is a Rockchip silicon engineer, he will know immediately what internal hardware signal triggers the latching of bit 16.
2. **Tomeu Vizoso**: Open the Mesa Draft MR and ask if his RK3588 bring-up encountered a similar "zero-point output stall" before the compute array was fully engaged.
