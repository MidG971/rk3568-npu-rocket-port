# NPU De-idle Fix Session Notes

## Current Status (2026-02-13 - SUCCESS! 🎉)
**NPU FULLY WORKING ON MAINLINE KERNEL 6.19!**

### Verified Working:
- ✅ RKNPU driver v0.9.8 loads and initializes
- ✅ DRM device opens (/dev/dri/card1)
- ✅ Driver ioctls work (test_hw_version, test_open)
- ✅ RKNN library (librknnrt.so) communicates with driver
- ✅ Model loading (rknn_init) succeeds
- ✅ **Actual NPU inference works!**

### Inference Performance:
- **YOLOv5s (640x640)**: ~48ms per inference
- Consistent timing across multiple runs
- Model: yolov5s-640-640.rknn (7.7MB)

### Minor Issue:
- Direct /dev/mem register access causes RCU stall (not a real problem - normal users use the driver)

### What Made It Work:
1. Manual PMU power-on in driver probe (voltage domain + power gate + bus de-idle)
2. `pd_ignore_unused clk_ignore_unused` kernel params
3. `regulator-always-on` on vdd_npu in DTS
4. FPGA_PLATFORM compile flag to bypass broken genpd path

---

## Session Summary (2026-02-10)

### What We Tried This Session

1. **Built mainline kernel 6.19 with RKNPU driver on BSP system** - Driver compiles and loads
2. **Added manual PMU power-on to rknpu_drv.c** - PMU shows NPU powered on and bus active
3. **Added vdd_npu regulator-always-on in DTS** - Prevents regulator from being disabled
4. **Added npu-supply reference to NPU node** - Links NPU to vdd_npu
5. **Moved PMU power-on to early probe** - Now happens before any hardware access
6. **Tested direct register access via /dev/mem** - Causes RCU stall (hang, not fault)

### Critical Discoveries

1. **BSP also crashes with direct /dev/mem NPU access when NPU is OFF**
   - BSP driver uses pm_runtime which triggers pm_domains power-on
   - pm_domains does more than just PMU power gate + bus de-idle
   - BSP ioctl wrapper: `rknpu_power_get()` → `rknpu_power_on()` → pm_runtime

2. **Our PMU power-on shows correct state but access still hangs**
   ```
   NPU bus: ACTIVE (bit2=0)
   NPU power: ON (bit1=0)
   ```
   Yet register read causes RCU stall (system hangs waiting for response)

3. **BSP pm_domains likely does memory isolation/repair**
   - BSP has extra registers: mem_status (0x1f8), repair_status (0x290), chain_status (0x1f0)
   - These may need to be written/checked during power-on

4. **BSP driver wraps all ioctls with power management**
   ```c
   #define RKNPU_IOCTL(func)                                    \
       static int __##func(...) {                               \
           rknpu_power_get(rknpu_dev);  /* Powers on NPU */     \
           ret = func(...);                                      \
           rknpu_power_put_delay(rknpu_dev);                    \
           return ret;                                           \
       }
   ```

---

## Driver Modifications Made

### rknpu_drv.c Changes (on NVMe: /mnt/nvme/linux-6.18/drivers/rknpu/)

1. **Added PMU power-on function** (after line 55):
```c
#ifdef FPGA_PLATFORM
#include <linux/io.h>

#define PMU_BASE                0xfdd90000
#define PMU_PWR_GATE_SFTCON     0xa0
#define PMU_BUS_IDLE_SFTCON0    0x50
#define PMU_BUS_IDLE_ST         0x68
#define PMU_PWR_DWN_ST          0x98
#define PMU_VOL_GATE_SFTCON     0xa8  /* NEW: Voltage domain control */

#define NPU_PWR_BIT             BIT(1)
#define NPU_IDLE_BIT            BIT(2)
#define NPU_VOL_BIT             BIT(1)  /* NEW: VD_NPU enable bit */

static void __iomem *pmu_base;

static int rknpu_pmu_power_on(struct device *dev)
{
    u32 val;
    int timeout = 1000;

    if (!pmu_base) {
        pmu_base = ioremap(PMU_BASE, 0x1000);
        if (!pmu_base) {
            dev_err(dev, "Failed to map PMU registers\n");
            return -ENOMEM;
        }
    }

    /* Step 1: Enable NPU voltage domain (VD_NPU) */
    val = readl(pmu_base + PMU_VOL_GATE_SFTCON);
    dev_info(dev, "VOL_GATE_SFTCON before: 0x%x\n", val);
    if (!(val & NPU_VOL_BIT)) {
        /* VD_NPU not enabled, enable it now */
        writel((NPU_VOL_BIT << 16) | NPU_VOL_BIT, pmu_base + PMU_VOL_GATE_SFTCON);
        udelay(10);  /* Small delay for voltage to stabilize */
        val = readl(pmu_base + PMU_VOL_GATE_SFTCON);
        dev_info(dev, "VOL_GATE_SFTCON after enable: 0x%x\n", val);
    }

    /* Step 2: Check if already powered on */
    val = readl(pmu_base + PMU_PWR_DWN_ST);
    if (!(val & NPU_PWR_BIT)) {
        dev_info(dev, "NPU already powered on (PWR_DWN_ST=0x%x)\n", val);
    } else {
        /* Power on NPU domain */
        writel((NPU_PWR_BIT << 16) | 0, pmu_base + PMU_PWR_GATE_SFTCON);

        while (timeout--) {
            val = readl(pmu_base + PMU_PWR_DWN_ST);
            if (!(val & NPU_PWR_BIT))
                break;
            udelay(1);
        }
        if (timeout <= 0) {
            dev_err(dev, "NPU power on timeout\n");
            return -ETIMEDOUT;
        }
        dev_info(dev, "NPU powered on (PWR_DWN_ST=0x%x)\n", val);
    }

    /* Step 3: De-idle NPU bus */
    writel((NPU_IDLE_BIT << 16) | 0, pmu_base + PMU_BUS_IDLE_SFTCON0);

    timeout = 1000;
    while (timeout--) {
        val = readl(pmu_base + PMU_BUS_IDLE_ST);
        if (!(val & NPU_IDLE_BIT))
            break;
        udelay(1);
    }
    if (timeout <= 0) {
        dev_err(dev, "NPU bus de-idle timeout\n");
        return -ETIMEDOUT;
    }
    dev_info(dev, "NPU bus de-idled (BUS_IDLE_ST=0x%x)\n", val);

    return 0;
}
#endif
```

2. **Added early power-on call in probe** (after dev_set_drvdata):
```c
#ifdef FPGA_PLATFORM
    ret = rknpu_pmu_power_on(dev);
    if (ret) {
        LOG_DEV_ERROR(dev, "failed to power on NPU domain\n");
        return ret;
    }
#endif
```

3. **Added power-on call in rknpu_power_on()** (after #endif for regulators):
```c
#ifdef FPGA_PLATFORM
    ret = rknpu_pmu_power_on(dev);
    if (ret)
        return ret;
#endif
```

### Device Tree Changes

1. **NPU node in rk356x-base.dtsi** (added before gpu node ~line 556):
```dts
rknpu: npu@fde40000 {
    compatible = "rockchip,rk3568-rknpu", "rockchip,rknpu";
    reg = <0x0 0xfde40000 0x0 0x10000>;
    interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_HIGH>;
    interrupt-names = "npu_irq";
    clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>,
             <&cru ACLK_NPU>, <&cru HCLK_NPU>;
    clock-names = "scmi_clk", "clk", "aclk", "hclk";
    assigned-clocks = <&cru CLK_NPU>;
    assigned-clock-rates = <600000000>;
    resets = <&cru SRST_A_NPU>, <&cru SRST_H_NPU>;
    reset-names = "srst_a", "srst_h";
    /* power-domains disabled - mainline pm-domains can't handle NPU */
    status = "disabled";
};
```

2. **Enable NPU in rk3568-rock-3b.dts**:
```dts
&rknpu {
    status = "okay";
    npu-supply = <&vdd_npu>;
};
```

3. **vdd_npu regulator always-on**:
```dts
vdd_npu: DCDC_REG4 {
    regulator-name = "vdd_npu";
    regulator-always-on;  /* Added */
    ...
};
```

---

## Tools Created (~/npu-tools/ on board)

| Tool | Purpose |
|------|---------|
| `check_pmu_bsp` | Read PMU registers and analyze NPU state |
| `check_pmu_bsp.c` | Source for check_pmu_bsp |
| `test_npu_reg` | Direct NPU register read via /dev/mem |
| `test_npu_reg.c` | Source for test_npu_reg |
| `test_open` | Test DRM device open and version ioctl |
| `test_open.c` | Source for test_open |
| `test_hw_version` | Test RKNPU_GET_HW_VERSION ioctl |
| `test_hw_version.c` | Source for test_hw_version |
| `pmu_power_on.c` | Reference PMU power-on code |

---

## Boot Configuration

### extlinux.conf Entries
```
default l1  # Mainline kernel

label l0
    menu label BSP 5.10
    linux /boot/vmlinuz-5.10.160-18-rk356x
    initrd /boot/initrd.img-5.10.160-18-rk356x
    fdtdir /usr/lib/linux-image-5.10.160-18-rk356x/
    fdtoverlays /boot/dtbo/rk3568-npu-enable.dtbo
    append root=UUID=... quiet ...

label l1
    menu label Mainline 6.19-rc5 with NPU driver
    linux /boot/Image-6.19-rc5-npu
    initrd /boot/initrd.img-6.19-rc5-npu
    fdt /boot/rk3568-rock-3b-6.19-npu.dtb
    append root=UUID=... rw loglevel=8 ignore_loglevel earlycon pd_ignore_unused clk_ignore_unused
```

### Kernel Files Installed
- `/boot/Image-6.19-rc5-npu` - Kernel image
- `/boot/initrd.img-6.19-rc5-npu` - initrd
- `/boot/rk3568-rock-3b-6.19-npu.dtb` - Device tree with NPU node
- `/lib/modules/6.19.0-rc5-00139-g983d014aafb1-dirty/` - Modules

---

## TRM Findings (2026-02-12)

### Key PMU Registers from TRM

1. **PMU_VOL_GATE_SFTCON (0x00A8)** - Voltage domain gate control
   - Bit 1 (`vd_npu_ena`): Enable PD_NPU as voltage domain
   - **Default = 0x1 (enabled)** - NPU operates as voltage domain by default
   - When enabled, NPU shares power with VD_NPU voltage rail

2. **PMU_BUS_IDLE_SFTCON0 (0x0050)** - Bus idle software control
   - Bit 2 (`idle_req_npu`): Controls NPU bus idle request
   - 0 = de-idle (active), 1 = idle
   - Write-mask protocol: `(mask << 16) | value`

3. **PMU_PWR_GATE_SFTCON (0x00A0)** - Power gate software control
   - Bit 1 (`pd_npu_dwn_ena`): Enable power down PD_NPU
   - Write-mask protocol

### NPU Clocks (CRU) - All Must Be Enabled
From TRM clock gating register:
- `clk_npu_src_en` (bit 0)
- `clk_npu_np5_en` (bit 1)
- `hclk_npu_pre_en` (bit 2)
- `pclk_npu_pre_en` (bit 3)
- `aclk_npu_biu_en` (bit 4)
- `hclk_npu_biu_en` (bit 5)
- `pclk_npu_biu_en` (bit 6)
- `aclk_npu_en` (bit 7)
- `hclk_npu_en` (bit 8)

### TRM Power-Down Sequence (documented)
1. Send BIU idle request via PMU_BUS_IDLE_SFTCON0
2. Query PMU_BUS_IDLE_ST until BIU is idle
3. Send power request via PMU_PWR_GATE_CON
4. Query PMU_PWR_DWN_ST to confirm power down

**Note**: Power-ON sequence not explicitly documented. Assumed to be reverse order.

---

## Research Agent Findings (2026-02-12)

### Key Finding: RK3588 GPU Power Domain Fix (Sebastian Reichel, Collabora)

The upstream fix for RK3588 GPU power domain (same class of problem) reveals the correct sequence:

1. **Regulator must be enabled FIRST** before power domain can work
2. Correct sequence: `regulator enable → clock enable → domain power-on → restore QoS`
3. The fix adds `devm_of_regulator_get` to the pm-domains driver
4. Without regulator, the "failed to get ack on domain" error occurs

**This likely applies to our RK3568 NPU problem!**

### Root Cause Analysis

The "failed to get ack on domain 'npu'" error pattern:
1. VDD_NPU regulator is not enabled when we try to power on the domain
2. The power-on sequence tries to de-idle the bus
3. The NPU hardware doesn't respond because it has no power
4. Timeout occurs, kernel panics or hangs

### Bootloader State Matters

From ODROID-M1 discussions:
- Petitboot may leave NPU in inconsistent state
- Workaround: `fw_setenv skip_spiboot true` to bypass Petitboot
- Our BSP U-Boot may need to enable VDD_NPU before kernel boots

### Open-Source "Rocket" Driver

Tomeu Vizoso's open-source RKNN driver:
- Merged into mainline kernel (expected in 6.18)
- Currently **only supports RK3588** NPU
- Uses DRM accel subsystem
- Not compatible with proprietary RKNN SDK
- RK3568 NPU support would need additional work

---

## Next Steps

### Priority 1: Enable Regulator in Power-On Sequence
The upstream RK3588 fix shows we need:
1. Get regulator handle via `devm_regulator_get()`
2. Call `regulator_enable()` BEFORE power domain operations
3. Add to our `rknpu_pmu_power_on()` function

### Priority 2: Verify Clocks Are Enabled
Add clock enable sequence:
1. Check CLKGATE_CON26 (0x368) NPU clock gates
2. Enable all NPU clocks before de-idle
3. Use write-mask protocol: `(mask << 16) | 0` to clear gate bits

### Priority 3: Check Voltage Domain Setting
Read PMU_VOL_GATE_SFTCON (0x00A8):
1. Bit 1 should be 1 (NPU as voltage domain)
2. If 0, write `(BIT(1) << 16) | BIT(1)` to enable

### Priority 4: Implement Proper Mainline Fix
See `PM_DOMAINS_COMPARISON.md` for full analysis. Key findings:

**BSP vs Mainline are nearly identical** - Same register offsets, same sequence.

**The real issue**: Genpd powers off "unused" domains at boot. When NPU driver
loads later, the power-on fails because clocks are gated and NIU doesn't respond.

**Recommended fix**:
1. Add `need_regulator = true` for RK3568 NPU domain in pm-domains.c
2. Add regulator binding in DT for NPU power domain
3. Optionally add `keepon_startup = true` to prevent genpd shutdown

**Files to modify**:
- `drivers/pmdomain/rockchip/pm-domains.c` - Add DOMAIN_RK3568_REG macro
- `arch/arm64/boot/dts/rockchip/rk356x.dtsi` - NPU power domain DT node

---

## PMU Register Reference (Extended)

| Register | Offset | Type | Purpose |
|----------|--------|------|---------|
| BUS_IDLE_CON0 | 0x40 | Config | Auto-idle configuration |
| BUS_IDLE_SFTCON0 | 0x50 | Write-mask | Software idle request |
| BUS_IDLE_ACK | 0x60 | Read-only | Idle acknowledge status |
| BUS_IDLE_ST | 0x68 | Read-only | Idle status |
| NOC_AUTO_CON0 | 0x70 | Write-mask | NOC auto-idle control |
| NOC_AUTO_CON1 | 0x74 | Write-mask | NOC auto-idle control |
| PWR_DWN_ST | 0x98 | Read-only | Power down status |
| PWR_GATE_SFTCON | 0xa0 | Write-mask | Power gate control |
| VOL_GATE_SFTCON | 0xa8 | Write-mask | Voltage gate control |
| CHAIN_ST | 0x1f0 | Read-only | Chain status (BSP) |
| MEM_ST | 0x1f8 | Read-only | Memory status (BSP) |
| REPAIR_ST | 0x290 | Read-only | Repair status (BSP) |

### NPU Bit Positions
- Power gate: bit 1 (PWR_GATE_SFTCON, PWR_DWN_ST)
- Bus idle: bit 2 (BUS_IDLE_*, NOC_AUTO_CON0)
- Voltage: bit 1 (VOL_GATE_SFTCON)

### CRU Clock Gate Registers (CRU base: 0xfdd20000)
| Register | Offset | NPU Bits |
|----------|--------|----------|
| CLKGATE_CON26 | 0x368 | NPU clocks (see TRM findings above) |
| CLKGATE_CON32 | 0x380 | Additional NPU clocks |
| SOFTRST_CON26 | 0x468 | NPU resets |

### Write-Mask Protocol
All PMU/CRU control registers use write-mask:
```
write_value = (mask << 16) | value
```
- To set bit N: `(BIT(N) << 16) | BIT(N)`
- To clear bit N: `(BIT(N) << 16) | 0`

---

---

## Pre-Mainline Testing Checklist

Before submitting patches, verify with stock U-Boot:
- [ ] NPU inference works (~48ms YOLOv5s)
- [ ] NVMe detection (currently broken on mainline)
- [ ] eth0 works (confirmed working)
- [ ] eth1 works (needs testing)
- [ ] HDMI output (needs testing)
- [ ] Reboot stability

## Reference Links
- Rockchip NPU SDK: https://github.com/rockchip-linux/rknpu2
- Rockchip kernel: https://github.com/rockchip-linux/kernel
- Mainline pm-domains: https://elixir.bootlin.com/linux/latest/source/drivers/pmdomain/rockchip/pm-domains.c
- RK3588 GPU Power Domain Fix: https://lore.kernel.org/lkml/20241211143044.9550-1-sebastian.reichel@collabora.com/T/
- RK3568 Power Domains Patch: https://lore.kernel.org/linux-devicetree/20210317025159.2248-1-zhangqing@rock-chips.com/T/
- Armbian RK3568 NPU Panic Issue: https://github.com/armbian/linux-rockchip/issues/297
- Open-source Rocket NPU Driver: https://blog.tomeuvizoso.net/2025/07/rockchip-npu-update-6-we-are-in-mainline.html
- PM-Domains Comparison: `PM_DOMAINS_COMPARISON.md` (local)
