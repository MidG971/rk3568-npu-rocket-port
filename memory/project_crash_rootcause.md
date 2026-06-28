---
name: Rocket IOMMU crash root cause and fix (Apr 18)
description: Hard lockup in rk_iommu_attach_device on every first job — root cause, fix applied, status
type: project
---

## Crash: Hard lockup in iommu_attach_group (first job, every boot to l3)

**Root cause**: `DL_FLAG_PM_RUNTIME` device link in `rk_iommu_probe_device` (rockchip-iommu.c:1210) makes
`pm_runtime_get_sync(npu_dev)` also resume `iommu_dev` as a supplier. When `rk_iommu_attach_device` then calls
`pm_runtime_get_if_in_use(iommu_dev)`, it returns 1 (device IS active). This bypasses the `hw_needs_enable=true`
deferred-init path and falls through to `rk_iommu_enable` → `rk_iommu_force_reset` on a CRU-reset IOMMU → AXI bus hangs.

**Fix applied** (Apr 18): In `rk_iommu_attach_device` (rockchip-iommu.c ~line 1099), after the
`pm_runtime_get_if_in_use > 0` check, added:
```c
if (iommu->hw_needs_enable) {
    pm_runtime_put(iommu->dev);
    return 0;
}
```
This defers hardware init to `rk_iommu_trigger_enable()` (called from rocket_job.c after NOC de-idle).

**Why:** When hardware hasn't been initialized (`hw_needs_enable=true`), calling `rk_iommu_enable` → `force_reset`
on a CRU-reset IOMMU hangs the AXI bus.

**How to apply:** Fix is in board source at `~/npu-port/linux-mainline/drivers/iommu/rockchip-iommu.c`.
Requires kernel Image rebuild (CONFIG_ROCKCHIP_IOMMU=y built-in). Build on LXC server (<TAILSCALE-IP>).

**Status (Apr 18)**: Fix applied to source, kernel Image rebuild in progress (rsync+cross-compile on LXC).
