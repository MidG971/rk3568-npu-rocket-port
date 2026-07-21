# ROCK 3B NPU Working on Mainline Linux 6.19 - Complete Guide

## TL;DR

**NPU inference now works on mainline kernel 6.19!** YOLOv5s runs at ~48ms per frame using the BSP RKNPU driver ported to mainline.

**Key requirements:**
- Kernel cmdline: `pd_ignore_unused clk_ignore_unused`
- DTS: `regulator-always-on` on vdd_npu
- Driver: Manual PMU power-on (FPGA_PLATFORM mode)
- No power-domains or IOMMU on NPU node

---

## Background

The Radxa ROCK 3B has an RK3568 SoC with a 1 TOPS NPU. While the BSP kernel 5.10 supports the NPU out of the box, getting it working on mainline Linux required significant investigation.

### The Problem

When booting mainline kernel with the RKNPU driver, any attempt to access NPU registers caused a **synchronous external abort**:

```
Internal error: synchronous external abort: 0x96000010
PC is at rknpu_get_hw_version+0x38/0x60
```

The root cause: the mainline `rockchip-pm-domains` driver cannot properly manage the RK3568 NPU power domain. It fails to de-idle the NPU bus:

```
rockchip-pm-domain: failed to get ack on domain 'npu', val=0x1ee
```

### Investigation Journey

We spent extensive time investigating:

1. **PMU Register Analysis** - The NPU was powered on (PWR_STATUS bit1=0) but the bus remained idle (BUS_IDLE_ST bit2=1)

2. **Write-Mask Protocol** - Discovered PMU registers use write-mask semantics (upper 16 bits = enable, lower 16 bits = value)

3. **All Software Paths Tried**:
   - Manual PMU register writes
   - genpd framework path
   - Clock ungating (CLKGATE_CON3)
   - Reset deassertion (SOFTRST_CON3)
   - Voltage domain enable (VOL_GATE_SFTCON)
   - NOC auto-idle disable (NOC_AUTO_CON0)

4. **BSP Comparison** - Booted BSP kernel 5.10 and confirmed NPU works perfectly with same hardware

5. **Root Cause Found** - The bootloader (U-Boot/TF-A) leaves the NPU in a state where it cannot respond to de-idle requests. BSP bootloader initializes NPU differently.

---

## The Solution

Since fixing the bootloader would require significant upstream work, we implemented a workaround that keeps the NPU powered and prevents genpd from touching it.

### Step 1: Device Tree Changes

Add these to your ROCK 3B DTS:

```dts
/* Keep vdd_npu always powered */
&vdd_npu {
    regulator-always-on;
    regulator-boot-on;
};

/* NPU node without power-domains (genpd can't handle it) */
/ {
    rknpu: npu@fde40000 {
        compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
        reg = <0x0 0xfde40000 0x0 0x10000>;
        interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_HIGH>;
        interrupt-names = "npu_irq";
        clocks = <&scmi_clk 2>, <&cru CLK_NPU>,
                 <&cru ACLK_NPU>, <&cru HCLK_NPU>;
        clock-names = "scmi_clk", "clk", "aclk", "hclk";
        assigned-clocks = <&cru CLK_NPU>;
        assigned-clock-rates = <600000000>;
        resets = <&cru SRST_A_NPU>, <&cru SRST_H_NPU>;
        reset-names = "srst_a", "srst_h";
        npu-supply = <&vdd_npu>;
        /* NO power-domains - mainline genpd fails with NPU */
        /* NO iommus - requires working power domain */
        status = "okay";
    };
};
```

### Step 2: Kernel Command Line

Add to `/boot/extlinux/extlinux.conf`:

```
append ... pd_ignore_unused clk_ignore_unused
```

This prevents genpd from powering off "unused" domains at boot.

### Step 3: Driver Modifications

The RKNPU driver needs modifications for mainline 6.19. Key changes:

1. **Compile with FPGA_PLATFORM** - This enables non-IOMMU mode and manual power control

2. **Add manual PMU power-on** in `rknpu_power_on()`:

```c
#ifdef FPGA_PLATFORM
static int rknpu_manual_power_on(struct device *dev)
{
    void __iomem *pmu;
    u32 val;
    int timeout = 1000;

    pmu = ioremap(0xfdd90000, 0x200);
    if (!pmu)
        return -ENOMEM;

    /* Disable NOC auto-idle for NPU (bit2) using write-mask */
    writel(0x00040000, pmu + 0x70);  /* NOC_AUTO_CON0 */

    /* Power on NPU domain (clear bit1) */
    writel(0x00020000, pmu + 0xa0);  /* PWR_GATE_SFTCON */

    /* Wait for power on */
    while (timeout-- > 0) {
        val = readl(pmu + 0x98);  /* PWR_DWN_ST */
        if (!(val & BIT(1)))
            break;
        udelay(1);
    }

    /* Request de-idle (clear bit2) */
    writel(0x00040000, pmu + 0x50);  /* BUS_IDLE_SFTCON0 */

    iounmap(pmu);
    return 0;
}
#endif
```

3. **API updates for kernel 6.19** (see table below)

### API Changes Reference (5.10 → 6.19)

| Old API | New API |
|---------|---------|
| `drm_driver.date` | Removed |
| `drm_driver.gem_prime_mmap` | Removed |
| `platform_driver.remove` returns int | Returns void |
| `MODULE_IMPORT_NS(X)` | `MODULE_IMPORT_NS("X")` |
| `hrtimer_init()` + `.function =` | `hrtimer_setup()` |
| `sg_is_dma_bus_address()` | `sg_dma_is_bus_address()` |
| `iommu_map_sg(d,i,s,n,p)` | `iommu_map_sg(d,i,s,n,p,GFP_KERNEL)` |

### Step 4: Compatibility Headers

Create stub headers in `drivers/rknpu/compat/` for BSP-only APIs:

- `rockchip_iommu.h` - IOMMU stubs
- `rockchip_opp_select.h` - OPP stubs
- `rockchip_system_monitor.h` - Thermal stubs
- `rockchip_ipa.h` - Power allocation stubs
- `rk-dma-heap.h` - DMA heap stubs

---

## Results

### Working Configuration

- **Kernel**: 6.19.0-rc5
- **Driver**: RKNPU v0.9.8 (ported from BSP)
- **Mode**: Non-IOMMU (FPGA_PLATFORM)
- **Userspace**: librknnrt.so v1.5.2

### Performance

```
$ ./rknn_yolov5_demo model/RK356X/yolov5s-640-640.rknn model/bus.jpg
model input num: 1, output num: 3
input tensors:
  index=0, name=images, n_dims=4, dims=[1, 640, 640, 3]
output tensors:
  index=0, name=output, n_dims=4, dims=[1, 255, 80, 80]
  ...
model first run speed: 48.23ms
```

**~48ms per frame for YOLOv5s** - comparable to BSP performance!

---

## Known Limitations

1. **No IOMMU** - Running in non-IOMMU mode (less secure, uses CMA memory)
2. **No devfreq** - Frequency scaling disabled (depends on BSP-only APIs)
3. **Workaround required** - Need kernel cmdline flags and DTS modifications
4. **Not mainline-ready** - This is a workaround, not a proper upstream solution

## Proper Upstream Fix

For a mainline-acceptable solution, the following would be needed:

1. **Fix rockchip-pm-domains driver** for RK3568 NPU power domain
2. **Or fix U-Boot/TF-A** to properly initialize NPU at boot
3. **Port the open-source "Rocket" driver** (currently RK3588 only) to RK3568

The open-source Rocket NPU driver by Tomeu Vizoso was merged in kernel 6.18, but it currently only supports RK3588 and uses a different userspace API (not compatible with RKNN SDK).

---

## Files and Resources

- **Project repository**: Contains all patches, DTS files, and compatibility headers
- **BSP RKNPU driver**: https://github.com/rockchip-linux/rknpu2
- **RKNN Toolkit**: https://github.com/rockchip-linux/rknn-toolkit2
- **Rocket driver (mainline)**: https://blog.tomeuvizoso.net/2025/07/rockchip-npu-update-6-we-are-in-mainline.html

---

## Acknowledgments

This work involved extensive debugging of PMU registers, power domain behavior, and bootloader initialization. Special thanks to the Armbian community (Issue #297) for documenting similar issues, and to Collabora/Tomeu Vizoso for the upstream Rocket driver work.

**Hardware**: Radxa ROCK 3B (RK3568, 4GB RAM)
**Kernel**: Mainline Linux 6.19-rc5
**Date**: February 2026

---

*Questions or improvements? Feel free to reply!*
