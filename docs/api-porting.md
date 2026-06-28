# API Porting Reference: BSP 5.10 → Mainline 6.19

## API Changes

| Old API (5.10) | New API (6.19) | File |
|----------------|----------------|------|
| `drm_driver.date` | Removed | rknpu_drv.c |
| `drm_driver.gem_prime_mmap` | Removed | rknpu_drv.c |
| `platform_driver.remove` returns int | Returns void | rknpu_drv.c |
| `MODULE_IMPORT_NS(X)` | `MODULE_IMPORT_NS("X")` | rknpu_drv.c |
| `hrtimer_init()` + `.function =` | `hrtimer_setup()` (combined) | rknpu_drv.c |
| `#include <linux/hrtimer.h>` | Must be explicit | rknpu_drv.c |
| `sg_is_dma_bus_address()` | `sg_dma_is_bus_address()` | rknpu_iommu.c |
| `iommu_map_sg(d, i, s, n, p)` | `iommu_map_sg(d, i, s, n, p, GFP_KERNEL)` | rknpu_iommu.c |
| `iommu_map(d, i, p, s, prot)` | `iommu_map(d, i, p, s, prot, GFP_KERNEL)` | rknpu_gem.c |
| `#include <linux/pfn_t.h>` | Removed; use plain `unsigned long pfn` | rknpu_gem.c |
| `__pfn_to_pfn_t(pfn, PFN_DEV)` | Just `pfn` | rknpu_gem.c |
| `VM_MAP` | `0` | rknpu_gem.c |
| `vmap()`/`vunmap()` | Need `#include <linux/vmalloc.h>` | rknpu_gem.c |
| `../drivers/devfreq/governor.h` | `linux/devfreq-governor.h` | rknpu_devfreq.c |

## Compatibility Headers

Located in `drivers/rknpu/compat/` on the ROCK 3B:

| Header | Purpose |
|--------|---------|
| `rockchip_iommu.h` | IOMMU function stubs (until IOMMU is re-enabled) |
| `rockchip_opp_select.h` | OPP/devfreq stubs |
| `rockchip_system_monitor.h` | Thermal monitor stubs |
| `rockchip_ipa.h` | Power allocation stubs |
| `rk-dma-heap.h` | DMA heap stubs |
| `version_compat_defs.h` | Version compat macros |

## devfreq (Mainline Implementation)

`rknpu_devfreq.c` uses standard mainline APIs only:
- `dev_pm_opp_of_add_table()` reads OPP table from DT (`operating-points-v2`)
- Custom `rknpu_ondemand` governor driven by `rknpu_dev->ondemand_freq` (set at job submission)
- `clk_set_rate(rknpu_dev->clks[0].clk, freq)` for SCMI clock scaling
- `regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX)` for voltage scaling
- `rknpu_devfreq_lock/unlock` use `devfreq->lock` mutex

OPP table in `rk3568-rock-3b.dts`: 200/400/600/800/1000 MHz at 825/875/925/950/1000 mV.
The BSP `rknpu_devfreq_bsp.c` is kept for reference but excluded from the build.
`rknpu_devfreq_stub.c` is deleted.

## Known Disabled Features

- **IOMMU** (`rknpu_mmu`): Node in base dtsi as `status = "disabled"`. Enabling causes
  kernel panic because DMA domain attach runs before `rknpu_noc_deidle()` in probe.

## Makefile Flags

```makefile
ccflags-y += -DCONFIG_ROCKCHIP_RKNPU_DRM_GEM  # use DRM GEM allocator
ccflags-y += -Werror
# FPGA_PLATFORM removed — driver runs on real hardware in mainline mode
```

## NPU Clock Architecture (RK3568)

- GPLL = 1188 MHz (not 1200!)
- NPU uses PVTPLL for rates 600–1000 MHz (ring oscillator at GRF+0x740)
- For rates ≤ 400 MHz: GPLL integer divider only
- PVTPLL table: 600(0xfb), 700(0xdb), 800(0xd3), 900(0xd3), 1000(0xd3)
- OTP calibration adjusts PVTPLL lengths per-chip
- `assigned-clocks` MUST include `&scmi_clk SCMI_CLK_NPU` to trigger PVTPLL init
- Also need `&cru CLK_NPU` for bus clock derivation (aclk_npu, hclk_npu)

## Device Tree: Key NPU Node Properties

```dts
&rknpu {
    status = "okay";
    rknpu-supply = <&vdd_npu>;
    power-domains = <&power RK3568_PD_NPU>;
    assigned-clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>;
    assigned-clock-rates = <1000000000>, <600000000>;
};

&vdd_npu {
    regulator-always-on;
    regulator-boot-on;
    regulator-min-microvolt = <1000000>;  /* 1000mV min for 1GHz */
};
```

Cmdline requires: `cma=128M` (default 32M is too small for NPU buffers)
