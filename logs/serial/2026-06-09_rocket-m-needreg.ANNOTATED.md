# rocket=m + need_regulator boot+teflon test — 2026-06-09 (annotated)

Raw complete log: `logs/serial/2026-06-09_rocket-m-needreg.log` (1102 lines — the one to send Chaoyi).

**Kernel:** `7.1.0-rc6-chaoyi-00011-ga31e2e6fae27` (#5 SMP PREEMPT Jun 9 12:40)
= v7.1-rc6 + Simon per-device IOMMU ops + v4 patches 1-10 (**need_regulator** variant:
`DOMAIN_M_R need_regulator=true` + `domain-supply=<&vdd_npu>`, no always-on) + **`CONFIG_DRM_ACCEL_ROCKET=m`**.
Boot entry **l6**, cmdline includes `fw_devlink=permissive`.

## RESULT: ✅ rocket=m AVOIDS the deadlock (Chaoyi's hypothesis CONFIRMED)
Boots clean to login; teflon runs **5 jobs with NO RCU stall / NO hang**. Contrast: the
`rocket=y` need_regulator kernel (2026-06-08) RCU-stalled + froze at the first job submit (~319s).
(out_sum still `26214400` = uniform-128 = the separate, unsolved HW compute gate — unrelated.)

## Key timestamps (answers Chaoyi's "when is the device-link error printed?")
```
[ 2.700618] vdd_npu: Bringing 500000uV into 825000-825000uV          <- rail up via regulator core
[ 2.749637] rockchip-pm-domain fdd90000...power-controller: Failed to create device link
            (0x180) with supplier 0-0020 for .../power-domain@6        <- AT PMDOMAIN PROBE (2.75s)
[ 2.945955] platform fde40000.npu: Adding to iommu group 3
[ 6.840374] rocket: loading out-of-tree module taints kernel.         <- rocket loads as MODULE @6.8s
[ 6.877647] [drm] Initialized rocket 0.0.0 for rknn on minor 0           (post-udev, I2C+reg core up)
[ 6.879950] rocket fde40000.npu: Rockchip NPU core 0 version: 0
```
- The `Failed to create device link ... 0-0020` warning is a **pmdomain-controller-probe event at
  2.75s**, *independent of rocket* (which loads 4s later at 6.8s). So it is NOT caused by the rocket
  probe; it's genpd failing to form the consumer→I2C-PMIC(0-0020) supplier link.
- Non-fatal here: the `vdd_npu` rail is up (825mV), and all NPU jobs run. No `failed to get ack on
  domain npu` NoC warning this boot (the always-on kernel had one).

## teflon (the actual test)
```
radxa@rock-3b:~$ lsmod | grep rocket
rocket                 24576  0
radxa@rock-3b:~$ sudo dmesg -C; python3 ~/npu-debug/teflon_test.py
Run 0..4: elapsed ~530-543ms, out_sum=26214400, "NPU job timed out"  (no hang, no RCU stall)
```

## v4 implication
- **KEEP patches 9+10 (need_regulator + domain-supply).** Reverses the 2026-06-08 "drop patch 10 /
  revert to always-on" decision. need_regulator works on RK3568 in the normal `rocket=m` config.
- Open follow-up (disclose to Chaoyi / pmdomain): the `Failed to create device link 0-0020` warning
  + the `fw_devlink=permissive` reliance — is that ordering issue benign, or is there a clean fix
  (so it works without `permissive` and `=y` doesn't early-probe-deadlock)?
