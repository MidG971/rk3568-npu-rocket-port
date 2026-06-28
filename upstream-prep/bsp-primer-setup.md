# BSP-primer swap setup — RK3568 engage test (the decisive remaining test)

GOAL: prime the NPU via the vendor rknn driver (engage + run a job), then `rmmod rknpu; modprobe rocket`
WITHOUT a power cycle, submit, and check engage (S_POINTER bit16). If primed-rocket engages → gate =
one-time power-up/init state; if not → rocket does something wrong at submit. (gahingwoo: RK3576 engages
COLD, so our gate is RK3568-specific init state — this test localizes it.)

## Obstacle + solution
- Both drivers must be MODULES on one kernel (currently CONFIG_RKNPU=y built-in, rocket=m).
- rknpu + rocket bind DIFFERENT compatibles + reg layouts on the same npu@fde40000 node:
  - rknpu: compatible "rk3568-rknpu"/"rknpu", reg = SINGLE <0xfde40000 0x10000> (uses base[0]; needs
    full range for 0x8010/0xf008).
  - rocket: compatible "rk3568-rknn-core", reg = THREE byname "pc"/"cna"/"core"
    (<0xfde40000 0x1000>,<0xfde41000 0x1000>,<0xfde43000 0x1000>).
  - clocks/resets/iommu/power = IDENTICAL between the two. Only reg + compatible conflict.
- SOLUTION: make rocket bind the rknpu (1-region) DT + add a single-region reg fallback (pc/cna/core =
  base+0/0x1000/0x3000) — preserves the 3-region rocket DTB (l3) AND binds the 1-region rknpu DT.

## ✅ DONE 2026-06-24 (bounded code/config changes, `patch_primer_rocket.py`)
1. **rocket match table** += "rockchip,rk3568-rknpu" + "rockchip,rknpu" (rocket_drv.c) → rocket binds
   the rknpu node.
2. **rocket reg single-region fallback** (rocket_core.c): byname "pc" first; on IS_ERR →
   `devm_platform_ioremap_resource(pdev,0)` as base, pc=base, cna=base+0x1000, core=base+0x3000.
   (3-region l3 path unchanged.)
3. **CONFIG_RKNPU=y → =m** (.config); CONFIG_DRM_ACCEL_ROCKET=m already. Both modules now.

## ✅✅ SETUP COMPLETE 2026-06-24 — ready to TEST (boot l8)
- rocket co-load patch applied (match += rknpu; single-region fallback). CONFIG_RKNPU=m.
- **rknpu Makefile FIX (was the build blocker):** it listed each .o in `obj-$(CONFIG_RKNPU)` (built-in
  form) → as =m each .o became a SEPARATE .ko (missing MODULE_LICENSE + undefined cross-refs). Fixed to
  multi-file module: `rknpu-y := rknpu_drv.o ...; obj-$(CONFIG_RKNPU) += rknpu.o` → one rknpu.ko.
- Kernel built (DONE_RC=0): `Image` → `/boot/Image-6.19-primer`; `rknpu.ko` + `rocket.ko` (rocket binds
  rk3568-rknpu, 3× in strings); primer DTB → `/boot/rk3568-rock-3b-primer.dtb`.
- `make modules_install` (MODINST_RC=0, depmod) → both .ko in
  `/lib/modules/6.19.0-rc5-00003-gb16c04e5e619-dirty/kernel/drivers/{rknpu,accel/rocket}/`.
- Boot entry **l8** added (Image-6.19-primer + primer.dtb + `modprobe.blacklist=rknpu,rocket` so neither
  auto-loads → manual control). default still l3.
**▶ TEST (next): set default l8 + reboot (NEW kernel — reboot risk; panic=5 auto-reboots on panic).
On l8: `sudo modprobe rknpu` (binds rk3568-rknpu) → run a vendor job (prime: rknn engages+computes) →
`sudo rmmod rknpu` (NO power cycle) → `sudo modprobe rocket` (binds, single-region) → `rocket_replay`
→ check S_POINTER bit16 / out distinct. ENGAGES = gate is one-time power-up state; STALLS = rocket
wrong at submit. Restore default l3 after.**

## ⏳ (was) REMAINING SETUP
4. **Build kernel**: `make Image modules` (rknpu now =m → Image loses built-in rknpu; rknpu.ko + rocket.ko
   built). `make modules_install`; cp Image → /boot/Image-6.19-primer. (~3-4 min on board.)
5. **Primer DTB**: new `rk3568-rock-3b-primer.dts` = base rock-3b + &rknpu override:
   - compatible = "rockchip,rk3568-rknn-core", "rockchip,rk3568-rknpu", "rockchip,rknpu";
   - reg = <0x0 0xfde40000 0x0 0x10000>;  (1-region; rocket byname fails → single-region fallback;
     rknpu base[0]=full) — DROP reg-names.
   - KEEP base clocks/resets/iommu/power + interrupts + interrupt-names="npu_irq" (rknpu needs it).
   - ADD rocket extras: rockchip,pmu=<&pmu>; npu-supply=<&vdd_npu>; sram-supply=<&vdd_npu>;
     assigned-clocks/-rates; operating-points-v2=<&npu_opp_table>; status="okay".
   - Ensure npu_opp_table + vdd_npu exist (copy from rk3568-rock-3b-rocket.dts). Easiest base = the
     rocket DTS, then change &rknpu's compatible(+rknpu) + reg(1-region, drop reg-names) + restore
     interrupt-names. Build → rk3568-rock-3b-primer.dtb.
6. **Boot entry l8**: Image-6.19-primer + rk3568-rock-3b-primer.dtb (label l8).
7. **TEST**: boot l8 (both =m, NEITHER auto-binds). `modprobe rknpu` → binds (rk3568-rknpu) → run a job
   (prime: rknn engages+computes). `rmmod rknpu` (unbind, NO power cycle). `modprobe rocket` → binds
   (now matches rk3568-rknpu, single-region) → run rocket_replay → **check S_POINTER bit16 / out distinct.**
   - ENGAGES → gate = one-time power-up state the vendor establishes (then hunt the specific init).
   - STALLS → rocket does something wrong at submit (not power-up state).

## Risks/notes
- Both-=m kernel must still boot (l8); if it wedges, fall back l3/l1 (default stays l3).
- rknpu=m: does anything depend on rknpu built-in at boot? (genpd always-on etc. — should be fine.)
- modprobe ONE at a time (rmmod rknpu before modprobe rocket) to avoid bind ambiguity.
- rknpu.ko build: rknpu is in-tree (drivers/rknpu, CONFIG_RKNPU) → builds with the kernel.
- After test: restore default l3; this kernel/DTB are extra boot entries, don't disturb l1/l3.
- Board: l3 baseline, Tailscale <BOARD-TAILSCALE-IP> (office net eth0 10.160.1.60).
