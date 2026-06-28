# ROCK 3B NPU Driver Port to Mainline Kernel 6.19

## Status: ROCKET BUG B FIXED — BUG A (NPU SILENT) STILL BLOCKING INFERENCE (2026-04-15)

- RKNPU driver v0.9.8 loads and initializes on 6.19.0-rc5
- Non-IOMMU mode: YOLOv5s ~69ms at 1GHz/1000mV, MobileNetV1 ~6ms
- **IOMMU mode: WORKING** — MobileNetV1 7.98ms, YOLOv5s 56.7ms, correct outputs
- Boot entry **l3** for Rocket driver test (`Image-6.19-rocket`, `rk3568-rock-3b-6.19-rocket-fixed.dtb`)
- **Rocket 5-patch series ready** at `patches/rocket-rk3568/` — holding until inference test passes
- **IOMMU DTE confirmed sub-4GB**: DTE_ADDR=0x04508000 (new runs) — GFP_DMA32 working ✓
- **pc_data_amount_scale=1 confirmed correct**: AMOUNTS_field=0x81 = count-1 ✓
- **BUG B FIXED** (2026-04-15): IOMMU detach/attach cycle works; `rk_iommu_identity_attach` prints every cycle, no EBUSY
- **trigger_enable fix applied** (2026-04-15): `rk_iommu_trigger_enable()` called after `iommu_attach_group()` in `rocket_job_run`; AMOUNTS now clean (0x0081) vs dirty (0x4081) before fix
- **BUG A still blocking**: NPU hardware silent after correct register setup; CNA/CORE S_STATUS=0 at timeout
- **Regcmd pattern observed**: every odd 32-bit word = 0x02010000; first words end in 0x1040; scale=2 experiment pending

## Board

- **User/Password**: radxa/radxa
- **SoC**: RK3568
- **IP**: Dynamic — use `cat ~/.rock3b_ip` or `./find-rock3b.sh`
- **Primary interface**: USB Ethernet (enx000ec649ecfe, <BOARD-MAC>)

```bash
./rock3b-ssh.sh "uname -a"
ssh radxa@$(cat ~/.rock3b_ip)
scp file radxa@$(cat ~/.rock3b_ip):/path/
```

## Project Locations on Board

```
~/npu-port/
├── linux-mainline/         # Kernel source
│   ├── drivers/rknpu/      # NPU driver (our port)
│   │   └── compat/         # Compatibility headers
│   └── arch/arm64/boot/dts/rockchip/
├── rockchip-bsp/           # BSP kernel (reference)
~/uboot-mainline/           # Mainline U-Boot + TF-A
~/rknpu2/                   # RKNN SDK examples
~/spi-backup-20260213.bin   # BSP U-Boot SPI backup (16MB)
```

## Current Git State (kernel)

```
Repository: ~/npu-port/linux-mainline
Branch: rknpu-port-6.19
Author: Midgy BALON <midgy971@gmail.com>
```

## Build Commands

```bash
# Full kernel (native on board)
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= -j4"

# Driver only (quick compile check)
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= drivers/rknpu/"

# Rocket driver only
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= -j4 M=drivers/accel/rocket"

# DTB only
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= rockchip/rk3568-rock-3b.dtb"

# Install
ssh radxa@$IP "cd ~/npu-port/linux-mainline && sudo cp arch/arm64/boot/Image /boot/Image-6.19-genpd && sudo make ARCH=arm64 modules_install"
```

## Boot Config

- **l0**: BSP 5.10 rescue — `vmlinuz-5.10.160-18-rk356x` + BSP fdtdir
- **l1**: Mainline 6.19 IOMMU (**default**) — `Image-6.19` + `rk3568-rock-3b-6.19-iommu.dtb`
- **l2**: Mainline 6.19 non-IOMMU (fallback) — `Image-6.19` + `rk3568-rock-3b-6.19-npu.dtb`
- **l3**: Mainline 6.19 Rocket accel driver — `Image-6.19-rocket` + `rk3568-rock-3b-6.19-rocket-fixed.dtb`
  - Image-6.19-rocket: compile server build, has `DRM_ACCEL=y DRM_ACCEL_ROCKET=m`
  - Rocket DTB (fixed): `rknn_mmu_0` uses `rockchip,iommu` (v1 ops, GFP_DMA32)

The iommu DTB has `rknpu_mmu` node enabled (status = "okay").

## Current NPU Driver Architecture

### Key Design Decisions

**SCMI two-step rate change** in `rknpu_power_on`:
```c
// Force TF-A clk_npu_set_rate() to run (triggers PVTPLL init = NPU bus de-idle)
clk_set_rate(scmi_clk, 600000000UL);  // intermediate rate
clk_set_rate(scmi_clk, rate);          // target rate
```

**PMU de-idle** via `rknpu_noc_deidle()`:
- Disables NOC auto-idle for NPU: `writel((1<<18)|0, pmu + 0x70)`
- Powers on NPU domain: `writel((1<<17)|0, pmu + 0xa0)`
- Requests de-idle: `writel((1<<18)|0, pmu + 0x50)`
- Waits up to 1ms for `BUS_IDLE_ST` bit2 to clear

**pm-domains always_on**: `DOMAIN_RK3568_AO` in `pm-domains.c` — genpd callbacks
are no-ops for NPU domain (standard genpd can't manage NPU without SCMI+PMU sequence).

### DT: NPU Node

```dts
&rknpu {
    status = "okay";
    rknpu-supply = <&vdd_npu>;
    power-domains = <&power RK3568_PD_NPU>;
    assigned-clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>;
    assigned-clock-rates = <1000000000>, <600000000>;
    operating-points-v2 = <&npu_opp_table>;
};

/* OPP table in rk3568-rock-3b.dts: 200/400/600/800/1000 MHz at 825/875/925/950/1000 mV */

&vdd_npu {
    regulator-always-on;
    regulator-boot-on;
    regulator-min-microvolt = <825000>;
    regulator-max-microvolt = <1000000>;
};
```

### Modified Files on Board

- `drivers/rknpu/rknpu_drv.c` — FPGA_PLATFORM removed, `rknpu_noc_deidle()`, SCMI two-step
- `drivers/rknpu/rknpu_reset.c` — SCMI two-step + `rknpu_noc_deidle()` before IOMMU ops in soft_reset
- `drivers/rknpu/rknpu_debugger.c` — FPGA_PLATFORM removed
- `drivers/rknpu/rknpu_devfreq.c` — Mainline devfreq (replaced BSP stub)
- `drivers/rknpu/rknpu_devfreq_bsp.c` — BSP version kept for reference
- `drivers/iommu/rockchip-iommu.c` — `hw_needs_enable` flag + debug prints + `__maybe_unused` + **`iommu_data_ops_v2.gfp_flags = GFP_DMA32` (PTE=0 fix)**
- `drivers/pmdomain/rockchip/pm-domains.c` — `always_on` NPU domain
- `arch/arm64/boot/dts/rockchip/rk356x-base.dtsi` — NPU node + rknpu_mmu (disabled by default)
- `arch/arm64/boot/dts/rockchip/rk3568-rock-3b.dts` — NPU enable, OPP table, VDD_NPU

### Rocket Driver Modified Files on Board (Apr 15 2026)

- `drivers/accel/rocket/rocket_core.h` — `iommu_dev` field (for trigger_enable/disable) + `attached_domain` field + forward decls
- `drivers/accel/rocket/rocket_job.c` — `attached_domain` tracking, HW_SUBMIT debug, detach-before-attach,
  pm_runtime leak fix; **`rk_iommu_trigger_enable()` called after `iommu_attach_group()`** (Apr 15 patch);
  REGCMD tail dump (last 8 words) in hw_submit debug block
- `drivers/accel/rocket/rocket_drv.c` — `pc_data_amount_scale=1`, postclose detach, runtime_resume detach;
  **iommu_dev lookup** via `of_parse_phandle(node, "iommus", 0)` + `of_find_device_by_node()` in probe;
  **`rk_iommu_trigger_disable()` called before clock gate** in `rocket_device_runtime_suspend` (Apr 15 patch)
- rocket.ko rebuilt: Apr 15 16:25, installed at `/usr/lib/modules/6.19.0-rc5/kernel/drivers/accel/rocket/`
  **NOTE**: scale=2 and REGCMD tail dump changes did NOT take effect on last test run (module reload issue — see 10a)
- `drivers/iommu/rockchip-iommu.c` — `rk_iommu_trigger_enable` / `rk_iommu_trigger_disable` exported functions
  (now integrated: trigger_enable called from rocket_job.c, trigger_disable from rocket_drv.c runtime_suspend)

## Serial Console

**Port**: UART2, GPIO header, 3.3V — **1500000 baud**

```bash
# Capture with picocom (recommended — clean log file)
picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/$(date +%Y-%m-%d)_l3_<description>.log

# Quick monitor only
picocom -b 1500000 /dev/ttyUSB0
```

Logs go in `logs/serial/` — see `logs/serial/README.md` for naming convention, annotation format,
grep recipes, and index of all past logs.

Index of older ad-hoc logs at project root: `log4.txt` (IOMMU probe fix), `log5.txt` (INT_MASK wiring),
`log6.txt` (GFP_DMA32 fix ← YOLOv5s working), `last_boot_log.md` (Rocket first probe),
`bootn2.txt` (scale=1 + AMOUNTS=0x81), `bootn3.txt` (detach-before-attach).

## NPU Testing

```bash
# Check driver loaded
ssh radxa@$IP "dmesg | grep -i rknpu | tail -10"

# Check DRM devices
ssh radxa@$IP "ls -la /dev/dri/"

# YOLOv5s benchmark
ssh radxa@$IP "cd ~/rknpu2/examples/rknn_yolov5_demo && ./build/build_linux_aarch64/rknn_yolov5_demo model/RK3566_RK3568/yolov5s-640-640.rknn model/bus.jpg"

# Teflon inference test
ssh radxa@$IP "python3 /tmp/teflon_test.py"

# Check debugfs
ssh radxa@$IP "sudo cat /sys/kernel/debug/rknpu/version"
```

## Benchmarks (RK3568, YOLOv5s-640x640)

| NPU Freq | Kernel freq | ms/frame |
|----------|-------------|----------|
| 400 MHz | CRU/GPLL | ~120ms |
| 600 MHz | CRU/GPLL | ~82ms |
| 800 MHz | PVTPLL/SCMI | ~74ms |
| 1000 MHz | PVTPLL/SCMI (non-IOMMU) | ~69ms |
| 1000 MHz | PVTPLL/SCMI **IOMMU** | **~57ms** |

## PMU Write-Mask Protocol

Upper 16 bits = write-enable mask, lower 16 bits = value.
- SET bit N: `(1 << (N+16)) | (1 << N)`
- CLEAR bit N: `(1 << (N+16)) | 0`

NPU key bits: PWR_DWN_ST bit1 (0=ON), BUS_IDLE_ST bit2 (0=ACTIVE)

## Rocket Patch Series (Ready — Not Yet Sent)

**Location**: `patches/rocket-rk3568/` (5 patches + cover letter)

| Patch | File | Subject |
|-------|------|---------|
| 0/5 | 0000-cover-letter.patch | Cover letter |
| 1/5 | 0001-accel-rocket-Use-of_find_matching_node... | `for_each_of_allnodes` → `of_find_matching_node` (module fix) |
| 2/5 | 0002-accel-rocket-Add-support-for-Rockchip-RK3568... | `rk3568_noc_init()` + `rk3568_soc_data` |
| 3/5 | 0003-dt-bindings-npu-Add-RK3568... | Binding: add rk3568-rknn-core, rockchip,pmu, optional sram-supply |
| 4/5 | 0004-arm64-dts-rockchip-rk356x-Update-NPU-node... | rk356x-base.dtsi: Rocket node layout, dual label, 200MHz default |
| 5/5 | 0005-arm64-dts-rockchip-rk3568-rock-3b-Enable-NPU... | rock-3b.dts: npu-supply, drop operating-points-v2 |

**dtbs_check**: passes clean on rk3568-rock-3b.dtb and all rk356x boards.

**Blocking**: inference test on l3 boot entry before sending. Tomeu will ask.

**Recipients** (from cover letter):
- To: Tomeu Vizoso `<tomeu@tomeuvizoso.net>`, Oded Gabbay `<ogabbay@kernel.org>`
- Cc: Rob Herring, Krzysztof Kozlowski, Conor Dooley, Heiko Stuebner
- Cc: dri-devel, devicetree, linux-arm-kernel, linux-rockchip, linux-kernel

## IOMMU Upstream (Waiting on Simon's Patch)

- Original GFP_DMA32 patch **withdrawn** 2026-04-03 (Message-Id: `<20260331075010.1463-1-midgy971@gmail.com>`)
- Simon Xue (Rockchip) confirmed two IOMMU versions: v1.0 (rknpu_mmu 0xfde4b000, vicap_mmu 0xfdfe0800) and v2.0 (VOP2)
- v1.0 cannot place DTE (first-level page table) above 4GB; both versions can map 40-bit physical pages
- **Fix path**: wait for Simon's per-device-ops patch (`<20260310105303.128859-1-xxm@rock-chips.com>`), then:
  1. Add `rockchip,rk3568-iommu-v1` compatible with `.gfp_flags = GFP_DMA32`, `.dma_bit_mask = DMA_BIT_MASK(40)`
  2. Update rk356x-base.dtsi: use `rk3568-iommu-v1` for rknpu_mmu + vicap_mmu nodes
- Offered Tested-by on Simon's patch; reply sent 2026-04-03

## Next Steps

1. ✅ `pd_ignore_unused` removed — always_on handles it
2. ✅ FPGA_PLATFORM ifdefs removed, `rknpu_noc_deidle()` renamed
3. ✅ devfreq re-enabled with mainline OPP APIs (2026-03-23)
4. ✅ On-board NICs auto-load via dwmac-rk udev alias
5. ✅ IOMMU probe + enable flow works; soft_reset crash fixed (2026-03-24)
6. ✅ NPU INT_MASK bit 16 wired for IOMMU mode; rk_iommu_irq fires on fault (2026-03-27)
7. ✅ Fix IOMMU PTE=0 bug — `iommu_data_ops_v2.gfp_flags = GFP_DMA32` (2026-03-29)
   - Root cause: missing GFP_DMA32 caused SWIOTLB bounce buffers for page tables; DMA_TO_DEVICE sync zeroed PTEs
   - Fixed with one-line change in `drivers/iommu/rockchip-iommu.c`
   - IOMMU mode now fully working: YOLOv5s 56.7ms, MobileNetV1 7.98ms, correct outputs
8. ✅ Rocket driver probing on RK3568 (2026-04-03) — `/dev/accel/accel0` created
   - `rocket_device.c`: `for_each_of_allnodes` → `of_find_matching_node` (non-exported symbol fix)
   - `rocket_drv.c`: PVTPLL two-step rate change (600MHz→1GHz) before NOC de-idle
   - `rocket_drv.c`: `regmap_write(pmu, 0xa0, BIT(1+16))` — PWR_GATE_SFTCON explicit power-on
     (genpd `always_on` no-op skips this; NPU hardware still power-gated at boot)
   - Boot entry **l3**: `Image-6.19-rocket` + `rk3568-rock-3b-6.19-rocket-fixed.dtb`
   - DTS: `&rknpu` overridden with `compatible = "rockchip,rk3568-rknn-core"`, 3-region reg layout
   - `ROCKET_CLK_NPU_IDX 2` = index of "npu" (SCMI) clock in core->clks[] bulk array
9. ✅ **Fix IOMMU DTE bug** — fixed DTB deployed (2026-04-04/05)
   - Root cause: `rockchip,rk3568-iommu` → v2 ops → `gfp_flags=0` → DTE above 4GB on 8GB board
   - Symptom: `PC_INTERRUPT_RAW_STATUS_DMA_READ_ERROR` (bit 12 = 0x1000) on every NPU job
   - Fix: `rk3568-rock-3b-6.19-rocket-fixed.dtb` — `rknn_mmu_0` uses `rockchip,iommu` (v1 ops, GFP_DMA32)
   - **CONFIRMED WORKING** 2026-04-06: DTE_ADDR=0x044c8000 (sub-4GB), DMA_READ_ERROR no longer fires
10. ✅ **pc_data_amount_scale fix compiled** (2026-04-05, rocket.ko 16:51)
    - scale=1 for RK3568 confirmed: AMOUNTS_field=0x81 = (130-1) ✓
    - But two new bugs found — see 10a/10b
10a. ⏳ **BUG: NPU job doesn't execute** (IOMMU attaches + enabled OK, hardware still silent)
    - First job: rk_iommu_attach fires, DTE=0x04508000 (sub-4GB ✓), STATUS=0x1 (PAGING_ENABLED ✓)
    - Registers at submit: BASE=0x1592000 AMOUNTS=0x81 OPENA=0x1 TASKCON=0x1001 — all look correct
    - NPU never raises IRQ after 500ms; at timeout OP_EN=1 (still set), CNA/CORE S_STATUS=0 (idle)
    - TASKCON=0x00001001: bits 0+12 (bit 0 = ?, bit 12 = TASK_PP_EN)
    - **Deferred IOMMU enable identified and fixed** (Apr 15): `pm_runtime_get_if_in_use(iommu_dev)` returned 0
      at attach time → `hw_needs_enable=true` → DTE never written to hardware → silent page walk failure.
      Fix: call `rk_iommu_trigger_enable(core->iommu_dev)` after `iommu_attach_group()` in `rocket_job_run`.
      Evidence: AMOUNTS field cleaned up (0x0081 clean vs 0x4081 with dirty upper bits before fix).
      But NPU still silent after fix — trigger_enable resolved DTE write deferral but hw is still not executing.
    - **Regcmd pattern observation** (all 13 MobileNetV1 jobs, consistent):
      - `REGCMD[0..3]: XXXXXXXX 02010000 YYYYYYYY 02010000` — every odd 32-bit word = 0x02010000
      - First words end in 0x1040 (e.g., 0x002a1040, 0x00571040, 0x00841040...); varies per layer
      - Words at index 2,4,6: 0x00001104, 0x00001100, 0x0003100c or 0x0000100c
      - 0x02010000 may be a register address (PC DMA interprets regcmd as [addr, value] pairs)
    - **scale=2 experiment pending**: may be correct for RK3568 (same as RK3588). AMOUNTS=0x81 with scale=1
      means PC reads 130 64-bit entries. With scale=2 it would read 65 128-bit pairs (= 130 64-bit words too).
      But if the AMOUNTS formula counts differently, scale=2 may be needed.
      **Module reload issue**: second build with scale=2 + REGCMD tail dump code did NOT load (rmmod may have
      failed silently while module in use). Both .ko files show Apr 15 16:25 but only first build ran.
    - **Remaining hypotheses**:
      a) scale=2 needed: PC stops mid-regcmd due to wrong AMOUNTS; OP_EN never reached
      b) IOMMU page walk still failing despite trigger_enable (check PAGE_FAULT_ACTIVE bit 1 at timeout)
      c) NOC bus re-idles between noc_init and hw_submit; need re-de-idle per-job
    - **Next debug steps**:
      1. Fix module reload: `sudo rmmod rocket && lsmod | grep rocket` before modprobe; or reboot l3
      2. Verify tail dump runs + check last regcmd words for OP_EN broadcast pattern
      3. Try scale=2, check if AMOUNTS changes to 0x40 and if NPU executes
      4. Read IOMMU STATUS at timeout: if bit1 (PAGE_FAULT_ACTIVE) set → page walk still broken
10b. ✅ **FIXED: EBUSY loop after first job timeout** (2026-04-15)
    - Root cause confirmed: `rk_iommu_suspend` (or PM callback on reset) was resetting `iommu->domain`
      to identity_domain before `iommu_detach_group` in `rocket_reset` could run → early-return with no print
    - Fix: detach-before-attach in `rocket_job_run` + `attached_domain` tracking (Apr 6) ensures clean state;
      confirmed working Apr 15: `rk_iommu_identity_attach: detaching from rk_domain=...` now prints on every cycle
    - All 13 MobileNetV1 jobs cycle through attach/timeout/detach/attach without EBUSY
10c. ⏳ **NEXT SESSION PLAN** — BUG A debug with serial capture
    - Start picocom: `picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/$(date +%Y-%m-%d)_l3_buga-scale2.log`
    - Reboot to l3 (clean module load — don't rely on rmmod/modprobe)
    - Verify new module: dmesg should show `scale=1` and REGCMD tail dump entries
    - Run `python3 /tmp/teflon_test.py`, let first job fire
    - Check tail dump: do last 8 regcmd words contain sub-unit OP_EN broadcast?
    - Try scale=2 in `rk3568_soc_data`: rebuild .ko, reload, check AMOUNTS=0x40
    - Add IOMMU STATUS readback at timeout: check PAGE_FAULT_ACTIVE (bit 1)
    - Save log as `logs/serial/YYYY-MM-DD_l3_buga-scale2.log` with annotation header
11. ⏳ **Send Rocket 5-patch series** — after inference confirmed (see patches/rocket-rk3568/)
    - Cover letter needs note: requires IOMMU v1.0 fix (Simon's patch) to work on 8GB+ boards
12. ⏳ Submit IOMMU v1.0 fix — after Simon's per-device-ops patch lands
13. PI6C20100 patch v6 sent 2026-03-27; awaiting Heiko Stuebner to apply
14. U-Boot debug cleanup: checkpoint letters A-J, e0-e5 in board_f.c; driver name prints in device.c

## Reference Docs

- `docs/npu-investigation.md` — Root cause analysis and investigation sessions
- `docs/api-porting.md` — API changes reference and compat headers
- `docs/patches.md` — Patch submission history

## Reference Links

- [RKNPU2 SDK](https://github.com/rockchip-linux/rknpu2)
- [Rocket NPU driver](https://docs.kernel.org/accel/rocket/index.html)
- [gated-fixed-clock binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/clock/gated-fixed-clock.yaml)
- [RK3568 TRM Part 1](https://opensource.rock-chips.com/images/2/26/Rockchip_RK3568_TRM_Part1_V1.3-20220930P.PDF)
- [PI6C20100 patch lore (v2)](https://lore.kernel.org/linux-rockchip/20260304132957.684616-1-midgy971@gmail.com/)
