# ROCK 3B NPU Project - Session Log & Reference

## Quick Reference

| Item | Value |
|------|-------|
| **Board IP** | 192.168.1.85 |
| **SSH User** | radxa |
| **SSH Password** | radxa |
| **Serial Console** | minicom (available) |
| **Kernel Version** | 6.19.0-rc5-00006-g76cd27aa0fe5-dirty |
| **SoC** | Rockchip RK3568 |
| **NPU Address** | 0xfde40000 |

## Connection Methods

### SSH
```bash
ssh radxa@192.168.1.85
```

### Serial Console
```bash
minicom  # Pre-configured for ROCK 3B serial
```

## Project Summary

Porting the Rockchip RKNPU driver from BSP kernel 5.10 to mainline Linux 6.19 for the Radxa ROCK 3B.

### Current Status: BLOCKER - NPU Power Domain Issue

The NPU driver loads but register access causes synchronous external abort. The mainline `rockchip-pm-domains` driver cannot properly manage the RK3568 NPU power domain.

---

## Test Results Tracking

### Session: 2026-02-03

#### What Was Tested

| Test | Result | Notes |
|------|--------|-------|
| Kernel 6.19.0-rc5 boot | PASS | Boots successfully with NPU driver |
| RKNPU driver probe | PASS | Driver initializes in non-iommu mode |
| NPU IOMMU enabled | FAIL | Causes kernel panic at boot (rk_iommu_enable_stall crash) |
| NPU power-domains DT | FAIL | `rockchip-pm-domains` fails: "failed to get ack on domain 'npu', val=0x1fe" |
| Manual PMU power-on | PARTIAL | PMU says "already on" but bus remains idled, register access still faults |
| rknn_yolov5_demo test | FAIL | Segfaults - triggers sync abort in `rknpu_get_hw_version` |
| NVMe via PCIe | PASS | Working |
| USB Ethernet | PASS | Working (primary connection method) |
| Onboard eth0/eth1 | FAIL | Not working at current location |
| GPU (panfrost) | PASS | Mali G52 detected and initialized |

#### Key Observations from Logs

1. **bootn2.txt** - Boot with IOMMU enabled in DT:
   - `rk_iommu_enable_stall` crashes because NPU registers are inaccessible
   - Kernel panic: "Attempted to kill init!"
   - NPU power domain off when IOMMU tries to access its registers

2. **bootn3.txt** - Boot with IOMMU/power-domains disabled:
   - Driver probes successfully: `RKNPU fde40000.npu: using non-iommu mode`
   - `[drm] Initialized rknpu 0.9.8 for fde40000.npu on minor 1`
   - BUT: `genpd_power_off_work_fn` crashes when trying to power off "unused" domain
   - Error: `rockchip-pm-domain: failed to get ack on domain 'npu', val=0x1fe`

3. **log4.txt, log5.txt** - Userspace test attempts:
   - Running `rknn_yolov5_demo` triggers sync abort
   - Crash in `rknpu_get_hw_version+0x8` (register read at offset 0x0)
   - Bus ack after manual de-idle shows 0x1ec but still faults

4. **log6.txt** - Reboot log:
   - Slow reboot due to systemd process timeout (unrelated to NPU)

---

## Root Cause Analysis

### The Problem

When userspace issues an ioctl, the driver tries to read NPU registers at 0xfde40000. The SoC bus rejects the read (synchronous external abort) because the NPU hardware is inaccessible.

### Why It Happens

1. Mainline `rockchip-pm-domains.c` tries to manage NPU power domain
2. During boot, genpd runs "Disabling unused power domains"
3. NPU domain gets powered off because no consumer holds a reference
4. Even with manual PMU writes, genpd re-idles the bus

### PMU Register State (from logs)

```
PMU base: 0xfdd90000
PWR_STATUS (0x98): NPU bit1=0 means powered ON
BUS_REQ    (0x50): NPU bit2=1 means idle REQUESTED
BUS_IDLE   (0x68): NPU bit2=1 means bus is IDLE
BUS_ACK    (0x60): NPU bit2=1 means idle ACK'd (val=0x1ee or 0x1fe)
```

The NPU is powered but the **bus is idled** - register access is blocked at NoC level.

### BSP vs Mainline pm_domains Differences

| Feature | BSP | Mainline |
|---------|-----|----------|
| Write-mask registers | Yes (`pwr_w_mask`) | No |
| Chain status checking | Yes (0x1f0) | No |
| Memory repair status | Yes (0x290) | No |
| Bus idle/ack protocol | Works | Fails for NPU |

---

## Known Working Configurations

### Current Boot Config (Working but NPU inaccessible)

**extlinux.conf:**
```
label l1
    menu label Debian GNU/Linux 6.19-rc5
    kernel /boot/Image-6.19-rc5
    initrd /boot/initrd.img-6.19.0-rc5-00005-g6e7ae836d989
    fdt /boot/rk3568-rock-3b-6.19-mainline.dtb
    append root=UUID=d7611020-2d80-4f32-b67d-5fb55de6949d rw loglevel=8 ignore_loglevel earlycon console=ttyFIQ0,1500000n8 console=ttyS2,1500000n8
```

**Device Tree (NPU node in rk356x-base.dtsi):**
```dts
rknpu: npu@fde40000 {
    compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
    reg = <0x0 0xfde40000 0x0 0x10000>;
    interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>,
             <&cru ACLK_NPU>, <&cru HCLK_NPU>;
    clock-names = "scmi_clk", "clk", "aclk", "hclk";
    /* power-domains and iommus DISABLED - causes crashes */
    status = "disabled";
};
```

---

## Files in Project Directory

### Core Project Files
| File | Purpose |
|------|---------|
| `CLAUDE.md` | Main context document for Claude Code |
| `SESSION_LOG.md` | This file - test tracking & reference |
| `find-rock3b.sh` | Network discovery script |
| `rock3b-ssh.sh` | SSH wrapper with auto-discovery |
| `setup.sh` | One-time setup script |
| `continue-fixes.sh` | Apply remaining driver fixes |
| `npu_power_patch.py` | Python script to patch manual PMU power-on code |

### Boot Logs
| File | Purpose |
|------|---------|
| `last_boot_log.md` | Latest boot log with full dmesg output |
| `log_claude_first_boot.txt` | First boot attempt log |
| `bootn2.txt` | Boot log with IOMMU enabled (crash) |
| `bootn3.txt` | Boot log with IOMMU disabled (partial success) |
| `log4.txt` | Userspace test crash log |
| `log5.txt` | Manual power-on test log |
| `log6.txt` | Reboot shutdown log |

### Technical Reference (Gemini Extracted)
| File | Purpose |
|------|---------|
| `rk3568-gemini.md` | GPU/NPU documentation (French) - memory map, PMU, clocks |
| `rk3568-gemini2.md` | Register summary for Kernel 6.19 driver development |
| `RK3568_GPU_NPU_TRM_Extraction.md` | TRM extraction - MMIO, clocks, resets, power |
| `RK3568_GPU_NPU_TRM_Extraction_v2.md` | Same as above (duplicate) |
| `RK3568_Panfrost_Integration.md` | GPU (Mali-G52) Panfrost driver integration notes |
| `RK3568_Kernel_6.19_Checklist.md` | Kernel 6.19 API changes checklist |

### Code Skeletons
| File | Purpose |
|------|---------|
| `rk3568-gpu-npu.dtsi` | Device tree skeleton for GPU/NPU |
| `rk3568-gpu-npu_v2.dtsi` | Device tree skeleton v2 (same content) |
| `rk3568_npu.c` | Minimal NPU driver skeleton |
| `rk3568_npu_v2.c` | NPU driver skeleton (same content) |
| `rk3568_npu_adv.c` | Extended NPU driver skeleton with IRQ handling |

### Technical Reference Manual
| File | Purpose |
|------|---------|
| `trm/*.pdf` | RK3568 TRM Part 1 pages (split PDF) |

---

## Hardware Reference (from TRM)

### Memory Map
| Block | Base Address | Size |
|-------|-------------|------|
| NPU | 0xFDE40000 | 128 KB |
| NPU PVTM | 0xFDE90000 | 64 KB |
| Mali-G52 GPU | 0xFDE60000 | 128 KB |
| GPU PVTM | 0xFDE80000 | 64 KB |
| PMU | 0xFDD90000 | 64 KB |
| CRU | 0xFDD20000 | 64 KB |
| SYS_GRF | 0xFDC60000 | 64 KB |

### Interrupts
| Module | GIC SPI |
|--------|---------|
| NPU | 151 |
| GPU | 148 |

### PMU Power Domain Registers (base 0xFDD90000)
| Register | Offset | NPU Bit | Description |
|----------|--------|---------|-------------|
| PMU_PWR_GATE_SFTCON | 0x00A0 | Bit 1 | Power gate control (write mask bit 17) |
| PMU_PWR_GATE_ST | 0x0098 | Bit 1 | Power status (0=ON, 1=OFF) |
| PMU_BUS_REQ | 0x0050 | Bit 2 | Bus idle request |
| PMU_BUS_IDLE | 0x0068 | Bit 2 | Bus idle status |
| PMU_BUS_ACK | 0x0060 | Bit 2 | Bus idle ack |

### Clock Configuration
**NPU Clocks:**
- `clk_npu` (Core) : CRU_CLKSEL_CON71, Source: NPLL
- `aclk_npu` (AXI Bus) : CRU_CLKSEL_CON70
- `hclk_npu` (AHB) : CRU_CLKSEL_CON70

**NPU Soft Resets:**
- CRU_SOFTRST_CON22, Bits 0-2: `srst_npu_a`, `srst_npu_h`, `srst_npu_p`

### Power Sequence (Driver Recommendation)
1. Enable power domain (PD_NPU via PMU)
2. Wait for power status (PMU_PWR_GATE_ST bit 1 = 0)
3. Clear bus idle request (PMU_BUS_REQ bit 2 = 0)
4. Wait for bus ack (PMU_BUS_ACK bit 2 = 0)
5. Deassert reset (CRU SRST_NPU)
6. Enable clocks (aclk, hclk, core)
7. Access NPU registers

---

## Next Steps (Priority Order)

1. **Fix mainline rockchip-pm-domains driver** for RK3568 NPU
   - Compare BSP `pm_domains.c` with mainline
   - Add write-mask support for RK3568
   - Fix bus idle/ack handshake sequence

2. **Alternative: Keep NPU always powered**
   - Prevent genpd from touching NPU domain
   - Not mainline-acceptable but works for testing

3. **Test with fixed pm_domains**
   - Re-enable `power-domains` in NPU DT node
   - Re-enable IOMMU
   - Run rknn_yolov5_demo

4. **Re-enable devfreq** (frequency scaling)
   - Currently disabled due to missing BSP APIs

---

## Useful Commands

### On ROCK 3B

```bash
# Check NPU driver status
dmesg | grep -i rknpu

# Check power domain status
cat /sys/kernel/debug/pm_genpd/pm_genpd_summary

# Check DRM devices
ls -la /dev/dri/

# Read PMU registers (requires devmem2)
devmem2 0xfdd90098  # PWR_STATUS
devmem2 0xfdd900a0  # PWR_GATE
devmem2 0xfdd90050  # BUS_REQ
devmem2 0xfdd90068  # BUS_IDLE
devmem2 0xfdd90060  # BUS_ACK

# Test NPU (will crash currently)
cd ~/rknn-toolkit2/rknpu2/examples/rknn_yolov5_demo
./rknn_yolov5_demo model/RK356X/yolov5s-640-640.rknn model/bus.jpg

# Rebuild kernel
cd ~/npu-port/linux-mainline
make ARCH=arm64 CROSS_COMPILE= -j4

# Install new kernel
sudo cp arch/arm64/boot/Image /boot/Image-6.19-rc5
sudo cp arch/arm64/boot/dts/rockchip/rk3568-rock-3b.dtb /boot/rk3568-rock-3b-6.19-mainline.dtb
sudo make ARCH=arm64 modules_install
```

### From Development PC

```bash
# SSH to board
ssh radxa@192.168.1.85

# Copy file to board
scp localfile radxa@192.168.1.85:/path/

# Copy file from board
scp radxa@192.168.1.85:/path/file ./
```

---

## Session History

### 2026-02-03 (Session 2) - Reference File Creation

**Board Status:**
- Kernel: `6.19.0-rc5-00006-g76cd27aa0fe5-dirty #13`
- NPU driver output:
  ```
  RKNPU fde40000.npu: using non-iommu mode
  [drm] Initialized rknpu 0.9.8 for fde40000.npu on minor 1
  RKNPU fde40000.npu: PMU pwr_status=0x000000fd
  RKNPU fde40000.npu: De-idle timeout: ack=0x000001fe idle=0x000001fe
  ```
- DRM devices: card0 (panfrost), card1 (rknpu), renderD128, renderD129

**Current Failure Mode:**
- Driver probes but de-idle times out
- `ack=0x1fe` means NPU bus idle ack bit is still set
- genpd crash when trying to power off domain after probe

**Files Added to Reference:**
- Consolidated Gemini-extracted TRM documentation
- Driver skeleton files (rk3568_npu*.c)
- Device tree skeletons (rk3568-gpu-npu*.dtsi)
- Hardware reference section with register details

### 2026-02-03 (Session 3) - YAML Binding & Driver Comparison

**Completed:**
- Created DT binding: `Documentation/devicetree/bindings/npu/rockchip,rk3568-rknpu.yaml`
- Validated binding with yamllint, dt-doc-validate, dt-validate
- Committed to git: `1e2505e26 dt-bindings: npu: Add Rockchip RK3568 RKNPU binding`
- Compared squelette_driver.c with current rknpu_drv.c implementation

**Driver Comparison: squelette_driver.c vs rknpu_drv.c**

| Aspect | squelette_driver.c | rknpu_drv.c (current) |
|--------|-------------------|----------------------|
| **Clock handling** | `clk_bulk_data clks[3]` with explicit IDs ("aclk", "hclk", "core") + `devm_clk_bulk_get()` | `devm_clk_bulk_get_all()` auto-discovers from DT |
| **Reset handling** | Single `reset_control *rst` + `devm_reset_control_get_exclusive()` | Array `reset_control **srsts` + `devm_reset_control_get_exclusive_by_index()` for multiple resets |
| **Runtime PM** | `pm_runtime_resume_and_get()` + autosuspend (100ms) | `pm_runtime_get_sync()` + manual refcount + delayed workqueue (3s) |
| **Power on** | `clk_bulk_prepare_enable()` + `reset_control_deassert()` | Complex: regulators + clocks + manual PMU writes + optional genpd domains |
| **Power off** | `reset_control_assert()` + `clk_bulk_disable_unprepare()` | IOMMU state check + multiple genpd domains + clocks + regulators |
| **Complexity** | ~130 lines, minimal | ~1800 lines, full-featured |

**Key observations:**
1. squelette uses modern kernel 6.19 APIs correctly (`pm_runtime_resume_and_get`)
2. rknpu already uses `devm_clk_bulk_get_all()` which is simpler than squelette's explicit IDs
3. rknpu's `FPGA_PLATFORM` code path adds manual PMU power control (workaround for mainline)
4. rknpu handles multiple NPU cores (RK3588 has 3), optional IOMMU, regulators, DRM/GEM
5. Neither approach solves the core issue: mainline pm_domains cannot de-idle NPU bus

**rknpu_drv.c manual power-on sequence (FPGA_PLATFORM):**
```c
// In rknpu_manual_power_on():
1. Check PMU_PWR_STATUS, power on if needed (write-mask: bit<<16)
2. Deassert NPU reset via CRU
3. Clear bus idle request (PMU_BUS_REQ, write-mask)
4. Wait for ack (PMU_BUS_ACK bit2=0) -- THIS TIMES OUT
```

### 2026-02-03 (Session 2) - Reference File Creation

**Board Status:**
- Kernel: `6.19.0-rc5-00006-g76cd27aa0fe5-dirty #13`
- NPU driver output: De-idle timeout with ack=0x1fe

**Files Added to Reference:**
- Consolidated Gemini-extracted TRM documentation
- Driver skeleton files, device tree skeletons
- Hardware reference section with register details

### 2026-02-03 (Session 1) - Initial Session Log Creation

- Created SESSION_LOG.md from existing project files
- Board confirmed at IP 192.168.1.85
- Serial console available via minicom
- Documented current blocker: NPU power domain management
- Consolidated test results from log files

---

## Current Blocker Summary

The NPU power domain de-idle sequence fails on mainline:

```
PMU pwr_status=0x000000fd  --> NPU powered (bit1=0)
De-idle timeout: ack=0x000001fe idle=0x000001fe  --> Bus still idled (bit2=1)
```

The mainline `rockchip-pm-domains` driver uses `regmap_update_bits()` which doesn't work correctly with RK3568's write-mask register protocol. The BSP uses direct writes with `mask << 16 | value` format.

**Fix needed:** Patch `drivers/soc/rockchip/pm_domains.c` to use proper write-mask protocol for RK3568.

---

*Last updated: 2026-02-03 (Session 3)*
