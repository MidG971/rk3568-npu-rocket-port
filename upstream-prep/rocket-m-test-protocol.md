# rocket=m test protocol (Chaoyi's hypothesis)

**Question being answered:** does building `rocket` as a **module** (loads late, after the
I2C PMIC + regulator core are up) avoid the RCU stall that `rocket=y` (built-in, probes early)
hit on the `need_regulator` kernel? Chaoyi asked for this + the **complete boot log**.

Kernel under test: `7.1.0-rc6-chaoyi-00011-ga31e2e6fae27`
= v7.1-rc6 + Simon per-device IOMMU ops + clean v4 (1–10, **need_regulator** variant:
`DOMAIN_M_R need_regulator=true` + `domain-supply=<&vdd_npu>`, **no** always-on) + `rocket=m`.
Boot entry: **l6** (default stays l3; l6 must be selected manually at U-Boot).

⚠️ **Remote board, may hang.** Be on the serial console; a hang needs a physical power-cycle.

## Steps (on the board, serial console)

1. **Start a full serial capture** (this IS the "complete boot log" Chaoyi wants):
   ```
   picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/$(date +%F)_rocket-m-needreg.log
   ```
2. **Reboot and pick l6** at the U-Boot/extlinux menu (do NOT change the default).
3. **Watch the boot log** for these, and note the **timestamp** of each:
   - `rockchip-pm-domain ...: Failed to create device link (0x180) with supplier 0-0020 ...`
     (this is the I2C-PMIC link warning Chaoyi asked "when is it printed?")
   - `rocket fde40000.npu: Rockchip NPU core 0 version: 0` (rocket probed — note: with `=m`
     this happens LATE, after udev autoloads it, not in early initcalls)
   - any RCU stall / hang during boot (with `=y` it hit at job submit, not boot)
4. **Confirm rocket loaded as a module:**
   ```
   lsmod | grep rocket          # should show rocket
   ls -la /dev/accel/            # accel0 should exist
   ```
   If `/dev/accel/accel0` is missing, manually: `sudo modprobe rocket` and watch dmesg.
5. **THE TEST — run an NPU job:**
   ```
   sudo dmesg -C
   python3 ~/npu-debug/teflon_test.py
   ```
   - **If it completes** (prints `out_sum=...` ×5, no hang) → `=m` AVOIDS the deadlock.
     need_regulator is viable → v4 keeps patches 9+10. (out_sum will still be 26214400 =
     uniform-128; that's the separate compute gate, expected.)
   - **If the board hangs / RCU-stalls** → `=m` does NOT help; the deadlock is in the
     genpd→I2C-regulator path regardless of probe timing → keep vdd_npu always-on, drop
     patch 10. Capture the stall trace, then power-cycle.
6. **Recover:** if it hung, power-cycle; it comes back on **l3** (default).

## What to report back
- The complete boot log file (`logs/serial/..._rocket-m-needreg.log`).
- Whether teflon completed or hung.
- The timestamp + context of the `Failed to create device link ... 0-0020` warning.
- `lsmod | grep rocket` + `dmesg | grep -iE "rocket|rknn|power-domain|rcu"` after the run.

I'll fold the outcome into the reply to Chaoyi and the v4 patch-9/10 decision.
