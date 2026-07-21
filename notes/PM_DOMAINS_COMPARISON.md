# Rockchip PM-Domains: BSP vs Mainline Comparison

## Purpose
Compare BSP kernel 5.10 `pm_domains.c` with mainline Linux `pm-domains.c` to identify
what needs to be fixed for proper RK3568 NPU power domain support.

## Summary of Differences

| Feature | BSP (5.10) | Mainline (6.x) | Status |
|---------|------------|----------------|--------|
| Basic power on/off | Yes | Yes | Same |
| Idle request/ack | Yes | Yes | Same |
| Write-mask protocol | Yes | Yes | Same |
| Regulator support | Yes | Yes (recently added) | OK |
| Memory status registers | Defined but unused for RK3568 | Defined but unused | Same |
| Clock ungating | Yes | Yes | Same |
| QoS save/restore | Yes | Yes | Same |

## Register Offsets (Identical)

Both BSP and mainline use the same register offsets for RK3568:

```c
static const struct rockchip_pmu_info rk3568_pmu = {
    .pwr_offset = 0xa0,      // PMU_PWR_GATE_SFTCON
    .status_offset = 0x98,   // PMU_PWR_DWN_ST
    .req_offset = 0x50,      // PMU_BUS_IDLE_SFTCON0
    .idle_offset = 0x68,     // PMU_BUS_IDLE_ST
    .ack_offset = 0x60,      // PMU_BUS_IDLE_ACK
};
```

## NPU Domain Definition (Identical)

```c
[RK3568_PD_NPU] = DOMAIN_RK3568("npu", BIT(1), BIT(2), false),
// Expands to:
// .pwr_mask = BIT(1), .pwr_w_mask = BIT(1) << 16
// .req_mask = BIT(2), .req_w_mask = BIT(2) << 16
// .status_mask = BIT(1), .idle_mask = BIT(2), .ack_mask = BIT(2)
```

## Power-On Sequence Comparison

### BSP Sequence (rockchip_pd_power with power_on=true):
1. Enable regulator (if pd->supply exists)
2. Enable clocks (`clk_bulk_enable`)
3. Ungate clocks (`rockchip_pmu_ungate_clk`)
4. Disable memory shutdown (if mem_sd_offset set)
5. Power on domain (`rockchip_do_pmu_set_power_domain`)
6. Memory reset/repair (if mem_status_mask set)
7. Clear idle request (`rockchip_pmu_set_idle_request(pd, false)`)
8. Restore QoS settings
9. Re-gate clocks, disable clocks

### Mainline Sequence (rockchip_pd_power with power_on=true):
1. Enable regulator (if pd->info->need_regulator)
2. Enable clocks (`clk_bulk_enable`)
3. Ungate clocks (`rockchip_pmu_ungate_clk`)
4. Power on domain (`rockchip_do_pmu_set_power_domain`)
5. Memory reset (if is_mem_on was true before)
6. Clear idle request (`rockchip_pmu_set_idle_request(pd, false)`)
7. Restore QoS settings
8. Re-gate clocks, disable clocks

### Key Observation
The sequences are nearly identical. Mainline now has regulator support (added by
Sebastian Reichel's RK3588 GPU fix patches).

## The Real Problem

After analysis, the issue is NOT a fundamental code difference. Both BSP and mainline
have the same logic for RK3568. The problem is:

### 1. Timing/State at Boot
When mainline kernel boots with stock U-Boot, the NPU power domain may be in an
inconsistent state. The BSP U-Boot and kernel are more tolerant of this.

### 2. Error Handling
Mainline panics on idle request timeout (if `panic_on_set_idle` is set), while
our test shows the idle/ack handshake works when we do it manually in the driver.

### 3. Genpd "Unused Domain" Shutdown
After boot, mainline genpd disables "unused" power domains. If the NPU driver hasn't
claimed the domain yet, genpd powers it off. Then when the driver tries to use it,
the power-on sequence fails because:
- The domain is already in an unknown state
- Clocks may be gated at CRU level
- The idle request fails to get ACK

## Working Solution (Current Hack)

Our working solution bypasses genpd entirely:
1. `FPGA_PLATFORM` flag in driver
2. Manual PMU register writes in `rknpu_pmu_power_on()`
3. `pd_ignore_unused clk_ignore_unused` kernel params
4. No `power-domains` property in NPU DT node

## Proper Mainline Fix Options

### Option A: Fix the Root Cause in rockchip-pm-domains.c
1. Add better error recovery when idle request fails
2. Don't panic, just return error and let driver handle it
3. Add clock enable before idle request (clocks must run for NIU to respond)

### Option B: Add RK3568 NPU-specific Handling
1. Define NPU as "always_on" or "keepon_startup" in domain definition
2. Add `need_regulator = true` for NPU domain
3. Ensure VDD_NPU regulator is enabled before any power operations

### Option C: Device Tree Solution
1. Mark NPU power domain with `rockchip,keep-power-on` or similar
2. Don't let genpd power off NPU automatically
3. Let driver manage power explicitly

## Recommended Fix for Mainline

The cleanest fix is Option B + Option C:

```c
// In pm-domains.c - change NPU definition
[RK3568_PD_NPU] = DOMAIN_RK3568_REG("npu", BIT(1), BIT(2), false),
// Where DOMAIN_RK3568_REG sets need_regulator = true

// Add new macro:
#define DOMAIN_RK3568_REG(name, pwr, req, wakeup)    \
    DOMAIN_M_R(name, pwr, pwr, req, req, req, wakeup, false)
// Where DOMAIN_M_R sets need_regulator flag
```

And in device tree:
```dts
&rknpu {
    power-domains = <&power RK3568_PD_NPU>;
    // Regulator binding for pm-domains to find
};
```

## Files to Modify for Proper Fix

1. `drivers/pmdomain/rockchip/pm-domains.c`:
   - Add `DOMAIN_RK3568_REG` macro or modify `rk3568_pm_domains[]`
   - Ensure regulator is fetched and enabled for NPU

2. `arch/arm64/boot/dts/rockchip/rk356x.dtsi`:
   - Add NPU power domain node with regulator binding
   - Reference power-domains in NPU node

3. `include/dt-bindings/power/rk3568-power.h`:
   - Already correct (RK3568_PD_NPU = 1)

## Testing the Fix

1. Remove our manual PMU power-on code from RKNPU driver
2. Re-enable `power-domains = <&power RK3568_PD_NPU>` in NPU DT node
3. Remove `pd_ignore_unused clk_ignore_unused` from kernel cmdline
4. Boot and check dmesg for pm-domain messages
5. Run `test_rknn_proper` to verify inference still works

## Drafted Patches

See `patches/` directory:

1. **0001-pmdomain-rockchip-Add-regulator-support-for-RK3568-NPU.patch**
   - Adds `DOMAIN_M_R` macro with regulator support
   - Adds `DOMAIN_RK3568_R` convenience macro
   - Changes NPU domain to use `DOMAIN_RK3568_R` with `need_regulator=true`

2. **0002-arm64-dts-rockchip-Add-NPU-regulator-for-RK3568.patch**
   - Shows how to add `domain-supply` property for NPU power domain
   - Board-specific: links NPU power domain to vdd_npu regulator

3. **0003-drm-rknpu-Use-standard-power-domain-management.patch**
   - Removes FPGA_PLATFORM manual PMU workaround
   - Uses standard pm_runtime with genpd
   - Optional: keep manual PMU as fallback

## Testing the Patches

1. Apply patches to mainline kernel
2. Remove `pd_ignore_unused clk_ignore_unused` from cmdline
3. Enable `power-domains = <&power RK3568_PD_NPU>` in NPU DT node
4. Add `domain-supply = <&vdd_npu>` to power domain node
5. Build and boot
6. Run `test_rknn_proper` - should get ~48ms inference

## References

- BSP: https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/soc/rockchip/pm_domains.c
- Mainline: https://github.com/torvalds/linux/blob/master/drivers/pmdomain/rockchip/pm-domains.c
- RK3588 GPU fix: https://lore.kernel.org/lkml/20241211143044.9550-1-sebastian.reichel@collabora.com/T/
