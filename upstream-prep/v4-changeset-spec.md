# RFC v4 changeset spec (v3 → v4)

Status: **✅✅ RE-REGENERATED 2026-06-10 → `upstream-prep/v4-patches-regen/` (cover + 9 patches),
base v7.1-rc6 (e43ffb69), LXC branch `rk3568-v4-regen`.** AUTO_GATING patch DROPPED (now upstream
via Sven/Simon "disable fetch dte time limit"), so v4 = 9 patches and **touches no IOMMU driver
file**. Validated: `git am` clean onto v7.1-rc6; **build OK** (rocket+pmdomain+rk3568-rock-3b.dtb,
BUILD_RC=0); **checkpatch --strict 0E/0W/4C** (4 CHECK = DOMAIN_* macro-arg-reuse, matches existing);
**dt_binding_check clean**. Cover rewritten: 0/9, deps = Simon per-device-ops [1] + merged
AUTO_GATING [2], changelog notes AUTO_GATING dropped + device-link benign + no fw_devlink=permissive.
One benign git-am "new blank line at EOF" on patch 1 (pre-existing, checkpatch-clean).
**✅ STYLE POLISH 2026-06-11 (final read-through vs maintainer feedback):** commit msgs 1-9 verified
clean (terse, HW-focused binding, WARN trace in p4) — no changes needed; patch 6 NOC→NoC (msg+YAML);
cover cuts = Status compressed to prose (no 3-bullet "split across three layers") + "Changes since v2"
dropped. **✅ PRE-SEND DIFF REVIEW 2026-06-12 (all Sashiko-bot fixes verified in diffs #1/2/4/5/6/8/9):**
FIXED — (a) P8 trailing-blank-line-at-EOF (the git-am whitespace warning); (b) cover IOMMU wording:
dropped the "NPU MMU is v2" claim that contradicted the dts's `rockchip,iommu` (v1-ops) compatible —
verified in driver that rockchip,iommu→iommu_data_ops_v1→GFP_DMA32 (32-bit DTE) is the CORRECT
compatible, rewrote to explain that + per-device-ops coexistence; (c) P3 clk_set_rate return now
dev_warn (was unchecked); (d) P8 commit msg now explains the vdd_npu 825mV floor + boot-on.
LEFT (with reason): P1 `struct rocket_core;` fwd-decl can't move (P2 hunk uses it as context); noc_init
name kept (defensible hook); SCMI assigned-rate 200M = defensible boot default (driver sets 1GHz).
✅ **vdd_npu VOLTAGE FIXED + BOARD-TESTED 2026-06-12 (P8):** the undervolt was real. Set vdd_npu
**min=max=1000000 (fixed 1000mV = the NPU's 1GHz operating voltage)**, NOT min825/max1000. Test
taught us: capping max alone left the rail at the MIN (the PMIC default 500mV clamps UP to min) →
825mV undervolt; only setting MIN=1000000 pins it. Booted l6 with the rebuilt dtb: rail
`min=max=cur=1000000` (`Bringing 500000uV into 1000000-1000000uV`), NPU probes (version 0), teflon
runs (out_sum 26214400 = compute gate, no regression). P8 commit msg updated (pin to 1000mV, fixed
compute rate, no devfreq scaling). Re-validated: AM clean, checkpatch 0E/0W/4C, dtb builds.
Board l6 dtb left at 1000mV (.bak825 = old); board restored to l3 default. **v4 ✅ SENT
2026-06-13** (user ran `git send-email` locally, fresh thread; cover with Sebastian fw_devlink
fold-in). Recipients: `upstream-prep/v4-recipients.txt`. TODO: revoke Gmail app pw; record
v4 cover Message-ID/lore URL; watch replies; do NOT push branch until maintainers okay.
Prior: 2026-06-08 → `upstream-prep/v4-patches/` (10 patches incl. AUTO_GATING bit-31) = SUPERSEDED.
(Below = the spec that drove it.)

Status (orig): **PREP ONLY — do not send.** Captures every v3→v4 delta so regenerating
the send-series is mechanical once (a) the board/LXC are free (the WiFi agent is
using them) and (b) Chaoyi answers the v2/DTE IOMMU question. Sources folded in:
Chaoyi Chen (Rockchip), Tomeu (maintainer), Sashiko bot, and the board-validated
fixes from 2026-06-05/07.

v3 as-sent = 9 patches, base v7.1-rc6 + Simon Xue per-device-ops [PATCH v2].
Files: `upstream-prep/v3-patches-final/`. Apply scripts (the validated deltas):
`/home/dio/.claude/jobs/cd5289c2/tmp/{patch_v4fixes.py,apply_chaoyi.py,apply_npu_alwayson.py}`.

---

## Per-patch v3 → v4

### Patch 1 — accel: rocket: Introduce per-SoC rocket_soc_data
- No functional change. Possibly carry the cores[] bound-check (see Patch 2).

### Patch 2 — accel: rocket: Derive DMA width and core count from match data
- **[bot #2/#8] cores[] OOB bound-check.** `rocket_probe` writes
  `rdev->cores[num_cores++]` per DT core node with no bound vs
  `soc_data->num_cores`. Add a check so a DT with more core nodes than
  `num_cores` can't overflow the fixed array. (Decide whether this lands in
  patch 1 or 2 — wherever cores[] iteration is introduced.)

### Patch 3 — accel: rocket: Add RK3568 SoC support
- **[bot #1 VERIFIED + #4] SCMI-clock-by-name + drop redundant PMU writes**
  (`patch_v4fixes.py`): rewrite `rk3568_noc_init()` to
  `of_clk_get_by_name(np,"npu")` + `clk_set_rate(600M)`/`clk_set_rate(1G)` +
  `clk_put` — the v3 code used `core->clks[ROCKET_RK3568_SCMI_CLK=2]` but
  `devm_clk_bulk_get` left `clks[].id` unset, so all entries resolved to aclk
  (it was rate-setting aclk, not the SCMI/PVTPLL clock). Also DROP the direct
  PMU de-idle regmap writes (genpd owns de-idle). **Board-verified:** NPU still
  probes; genpd-alone de-idle sufficient.
- **[bot #5/#6] noc_init error unwind.** On `rk3568_noc_init` failure,
  `rocket_core_init` must call `rocket_core_fini` (iommu_group_put,
  pm_runtime_disable) — v3 returns after pm_runtime_put_sync + rocket_job_fini
  but leaks the core_fini path.

### Patch 4 — accel: rocket: Reset the NPU before detaching the IOMMU on timeout
- Keep as-is (board-verified reset-before-detach WARN fix).

### Patch 5 — accel: rocket: Keep the IOMMU domain attached across jobs
- **[bot #2] clear attached_domain on reset** (`patch_v4fixes.py`): after
  `rocket_core_reset()` (which wipes the IOMMU DTE base), detach + clear
  `core->attached_domain` so the next job re-attaches/reprograms it; else
  attach-once skips re-attach → DMA fault after a hard reset. Board-verified
  logically; runtime path exercised by the job-timeout (compute gate).
- **[bot #9] fini ordering.** Stop the scheduler (rocket_job_fini) BEFORE the
  IOMMU teardown in rocket_core_fini to avoid a race/UAF if a job is in flight.

### Patch 6 — iommu/rockchip: AUTO_GATING  ← **DROP (resolved 2026-06-10)**
- **DECISION (2026-06-10, verified): DROP this patch entirely. Do NOT carry our own
  AUTO_GATING write in any form.** The correct bit-31 fix is **already merged upstream**:
  `[PATCH v2] iommu/rockchip: disable fetch dte time limit` (Sven Püschel, From: Simon
  Xue, msgid `20260428-spu-iommudtefix-v2-1-f592f579e508@pengutronix.de`). It adds
  `#define DISABLE_FETCH_DTE_TIME_LIMIT BIT(31)` and the exact `auto_gate |= BIT(31)` RMW
  Chaoyi gave us, in `rk_iommu_enable()` before `enable_paging`, **unconditionally for all
  rockchip IOMMUs** (squash of vendor 7f8158fb + 6a355e5f). **Joerg Roedel "Applied,
  thanks." 2026-06-02 → iommu/next (→ v7.2). NOT in v7.1-rc6.**
- So our earlier plan ("replace patch 6 with Chaoyi's bit-31 form + Suggested-by") is
  obsolete — it would duplicate the merged patch. The 2026-06-08 guess that "RGA needs
  this + merged" was the DTE/GFP_DMA32 fix already in 7.1-rc6 was WRONG (read the diff:
  it IS the AUTO_GATING bit-31 fix, and it is NOT in 7.1-rc6).
- **ACTION:** drop our `043be7a` (clear bit 1) and the `c79186d` "drop AUTO_GATING" commit
  both fall away; REBASE v4 onto a base that contains iommu/next (or linux-next). The NPU
  MMU then gets BIT(31) for free. On our HW it's a no-op anyway (l4 0x3 == l5 BIT(31),
  both fault-free), so no behaviour change.
- **Cover dependency list** = Simon Xue per-device-ops + this fetch-dte-time-limit patch
  (both Rockchip, both upstream-accepted). bit-31 = `DISABLE_FETCH_DTE_TIME_LIMIT`: without
  it the newer IOMMU rev blocks after 4 consecutive master DTE-fetches racing CPU iotlb
  zaps (same erratum RK3568/RK3588 as RV1126/RV1109).
- ⇒ **v4 IOMMU footprint = zero of our own patches.** Patch count drops accordingly
  (was 10 with this; now our series carries none of the AUTO_GATING change).

### Patch 7 — dt-bindings: npu: rockchip,rk3588-rknn-core: Add RK3568
- **[bot #7] make rockchip,pmu conditional-required for rk3568.** The if/then
  only requires sram-supply for rk3588; pmu is functionally mandatory for
  rk3568 (driver fails without it) but isn't in `required:` → invalid DTs pass.
  Add rockchip,pmu to the rk3568 conditional-required branch.

### Patch 8 — arm64: dts: rockchip: rk356x: Add the NPU and its IOMMU
- **[bot #10] node ordering** by unit address (npu@fde40000 / iommu@fde4b000
  before gpu@fde60000).
- **[NEW 2026-06-07, board-verified] Assign the CRU NPU clock too.** v3 sets only
  `assigned-clocks = <&scmi_clk SCMI_CLK_NPU>; assigned-clock-rates = <200000000>;`
  → the CRU `clk_npu`/`aclk_npu` (the NPU AXI bus clock) stay at the 12 MHz boot
  default (vs BSP's 200 MHz). Fix: `assigned-clocks = <&scmi_clk SCMI_CLK_NPU>,
  <&cru CLK_NPU>; assigned-clock-rates = <200000000>, <600000000>;` (matches BSP;
  brings aclk_npu 12→200 MHz). REAL latent bug. NOTE: it does NOT fix the compute
  gate (clocks then == BSP and conv still uniform-128 — clocks ruled out), but it's
  correct and belongs in the series. See [[compute-gate-pvtpll-hypothesis-2026-06-07]].
- **v2/DTE RESOLVED (Chaoyi 2026-06-08): "If Simon's method works, please use
  it."** → keep the v3 approach: NPU MMU via Simon Xue's per-device-ops series
  (the per-ops GFP_DMA32/32-bit-DTE infra is already in 7.1-rc6, lines 1353-1362;
  per-device ops lets the NPU MMU select the 32-bit-DTE ops without the global
  rk_ops coexist WARN). Cover Dependency section: keep, citing Simon's series.
  Patch 8 UNBLOCKED.

### Patch 9 — arm64: dts: rockchip: rk3568-rock-3b: Enable the NPU
- **REPLACE the vdd_npu always-on hack with proper genpd ownership (Chaoyi
  2026-06-08).** v3 marks vdd_npu regulator-always-on (because domain-supply
  deadlocked on the I2C PMIC). Chaoyi: set `need_regulator` for the RK3568 NPU
  pmdomain (like RK3588) + wire `domain-supply = <&vdd_npu>`; need_regulator
  changes the power-off/on sequencing (defaults domain off, enables regulator
  properly) and avoids the deadlock. Ref: lore 20251216055247.13150-1-rmxpzlb.
  → NEW v4 pmdomain patch (pm-domains.c: give RK3568_PD_NPU a regulator param /
  need_regulator=true; DOMAIN_RK3568 macro has no regulator arg today, so extend
  it or use a regulator-capable variant) + dts (domain-supply=<&vdd_npu>, drop
  regulator-always-on). MAY ALSO fix the NoC re-power de-idle gap (proper domain
  modelling). Keep: drop NPU pm_qos; node ordering.
  **⚠️ REVERSED 2026-06-08 — need_regulator HANGS (RCU stall) on JOB SUBMIT.** The
  v4 need_regulator kernel BOOTS clean + NPU probes, BUT teflon (NPU job) → RCU
  stall → board freeze (the boot-time "Failed to create device link with supplier
  0-0020 [rk809 I2C PMIC]" was the canary). Same I2C-PMIC genpd deadlock the
  always-on hack was built to dodge, just deferred boot→job. RK3568 vdd_npu is on
  the rk809 **I2C** PMIC; genpd driving it during the NPU pm_runtime cycle does an
  I2C op in a no-sleep context → RCU starvation. (Chaoyi's need_regulator likely
  works on RK3588 = non-I2C regulator.) **ACTION for v4: DROP patch 10
  (need_regulator) AND revert patch 9's domain-supply → keep vdd_npu
  regulator-always-on (the WORKING approach; l4 + build6 always-on both run teflon
  w/o hanging).** Report to Chaoyi/pmdomain: need_regulator RCU-stalls on RK3568
  (I2C PMIC) at job submit — deferred/async regulator path, or keep always-on?
  See [[chaoyi-rockchip-feedback-2026-06-05]], [[genpd-clean-bringup-2026-06-04]].
  **✅✅ RE-REVERSED 2026-06-09 — KEEP patches 9+10 (need_regulator).** The 2026-06-08
  RCU stall was an artifact of building `rocket=y` (built-in → early-initcall probe →
  genpd→I2C-PMIC regulator op before RCU/scheduler ready). Chaoyi's `rocket=m` suggestion
  is CONFIRMED: rebuilt 7.1-rc6 + v4(1-10, need_regulator) + `CONFIG_DRM_ACCEL_ROCKET=m`,
  booted on l6 → **boots clean + teflon runs 5 jobs, NO RCU stall / NO hang** (rocket
  autoloads @6.84s post-udev). `=m` is the upstream-normal config anyway. So **v4 KEEPS
  patch 9 (domain-supply, no always-on) + patch 10 (DOMAIN_M_R need_regulator).** Open
  follow-up to disclose: the `Failed to create device link 0-0020` warning prints at 2.75s
  (pmdomain probe, independent of rocket) and boot used `fw_devlink=permissive` — ask
  Chaoyi/pmdomain whether that ordering is benign or has a clean fix (so a `=y` build
  wouldn't early-probe-deadlock). Evidence: logs/serial/2026-06-09_rocket-m-needreg{.log,.ANNOTATED.md}.

---

## Cover letter v4 changelog (draft bullets)
- AUTO_GATING: **no longer carried by this series** (was our patch 6). The bit-31 fix
  is already merged upstream (iommu/rockchip: disable fetch dte time limit, Simon
  Xue/Sven Püschel, applied by Joerg Roedel 2026-06-02, iommu/next) — rebase past it,
  cite as a dependency.
- vdd_npu: proper genpd ownership via `need_regulator` + `domain-supply` (was
  regulator-always-on); Suggested-by Chaoyi Chen.
- rk3568 dts: assign CRU CLK_NPU (was 12MHz default; now 200MHz like BSP).
- rk3568 SoC: fetch the SCMI/PVTPLL clock by name; drop the redundant driver
  PMU de-idle (handled by the power domain).
- IOMMU: clear the attached domain on reset; fix core_init/fini error and
  teardown ordering.
- Binding: require rockchip,pmu for rk3568.
- Dependencies (both Rockchip, both upstream-accepted): (1) Simon Xue's per-device-ops
  series — confirmed by Chaoyi as the intended way to give the NPU MMU a 32-bit DTE
  while the SoC's other IOMMUs stay 40-bit; (2) the merged "disable fetch dte time
  limit" AUTO_GATING bit-31 fix (iommu/next).
- dts: order NPU/IOMMU nodes by unit address.
- IOMMU dependency section: **rework per Chaoyi** — NPU MMU is v2 with a 32-bit
  DTE cap (was framed as v1 + Simon's per-device-ops). [PENDING his answer]
- Status: still RFC; end-to-end inference still blocked on the HW compute gate
  (MAC/output never completes); IOMMU + bring-up handled.

---

## NOT in the series (separate / discussion)
- **genpd NPU-domain re-power de-idle fragility** (`apply_npu_alwayson.py` was the
  TEST workaround): late NPU probe (after genpd powers the unused domain off)
  fails to re-de-idle the NoC → IOMMU external abort. **Likely SUPERSEDED by the
  need_regulator fix (patch 9)** — proper domain modelling changes the power
  off/on sequencing; re-test whether ALWAYS_ON is still needed once need_regulator
  is in. If it's still needed, it's a Finley-Xiao question (Chaoyi deferred
  PVTPLL/NoC to him). NOT a v3 blocker (lean as-sent config probes early). Drop
  the ALWAYS_ON test hack from v4.
- Compute gate (uniform-128 / MAC never completes): the real merge blocker.
  TWO expert threads live: Tomeu (DPU output register 0x4020 — is it right for
  RK3568? cmdstream sent) + Finley Xiao (PVTPLL/NoC, Chaoyi CC'd him). Out of
  scope for the kernel series. Clocks RULED OUT.

---

## Regeneration procedure (mechanical, when board/LXC free + Chaoyi answered)
1. Fresh tree: rebase onto a base with iommu/next (has Simon's per-device-ops AND the
   merged AUTO_GATING bit-31 fix) — or v7.1-rc6 + Simon v2 if iommu/next not yet in
   linux-next, citing the AUTO_GATING patch as an additional dependency.
2. `git am` v3 patches 1-5,7-9; then fold the per-patch deltas above into the
   right commits (interactive rebase) — NOT a squashed commit.
3. Patch 6: **DROP** (AUTO_GATING bit-31 is upstream; do not carry our own).
4. `git format-patch -v4 --base=<base> --cover-letter`, write the changelog.
5. checkpatch + dtbs_check on the dts/binding.
6. Send only after USER review (standing rule: no send / no repo push until
   maintainers OK).
