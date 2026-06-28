# v5 plan — adopt Jonas Karlman's redirect + fold sashiko items

Status: DRAFT (board-validated 2026-06-13). NOT started. Wait for more human feedback
(esp. on the dts/pmdomain) before re-rolling — Jonas is the first human reply.

## Driver of the redesign: Jonas Karlman (v4 patch-8 reply, 2026-06-13)
[[jonas-v4-pvtpll-feedback-2026-06-13]]. Three asks, all adopted:
1. Drop `regulator-boot-on` (NPU not used in bootloader).
2. Don't pin `vdd_npu` to 1000/1000 mV — describe the HW, not a SW workaround.
3. **Drop PVTPLL for initial support.** Use CLK_NPU ≤400 MHz (or keep SCMI ≤400 MHz) to keep TF-A
   out of `PVTPLL_NEED` mode. Proper PVTPLL clk driver + OPP comes in a *future* series.

**Board-validated 2026-06-13:** NPU probes + submits fine at SCMI=396 MHz with zero PVTPLL
machinery — no regression vs the 1 GHz/PVTPLL path. (Also confirmed the compute gate is
independent of the compute clock — out_sum/executer unchanged — so dropping PVTPLL costs us
nothing on that front either.)

## Concrete v5 changes
(Refined by Jonas's 2nd reply on patch 7, 2026-06-13 — SCMI is a *virtual* clock that auto-routes
to CRU at ≤400 MHz / PVTPLL at ≥600 MHz; set ONLY the SCMI clk, don't drive CLK_NPU separately.)

### Clock — patch 7 (dts) + patch 3 (driver)
- **Set ONLY the SCMI NPU clk to 400 MHz** (≤400 keeps it on the plain CRU path, no PVTPLL):
  ```
  assigned-clocks = <&scmi_clk SCMI_CLK_NPU>;
  assigned-clock-rates = <400000000>;
  ```
  **Drop the `<&cru CLK_NPU>` assignment entirely** (driving CLK_NPU "outside SCMI control" is what
  Jonas flagged as strange). The SCMI clk brings the CRU bus clocks up implicitly — board-observed:
  at SCMI=396 MHz, `aclk_npu` came up at 600 MHz on its own, so our v4 "assign CLK_NPU for the
  12 MHz aclk" fix is unnecessary.
- **Drop `rk3568_noc_init` entirely** (patch 3) — the two-step rate dance AND the PMU NoC-de-idle
  poke. genpd de-idles the NoC at power-on ([[genpd_clean_bringup_2026_06_04]]). Patch 3 shrinks to
  just per-SoC data (dma_bits=32, pc_data_amount_scale=1, compatible). **VERIFY on 7.1-rc6** that
  the NPU still probes+submits with noc_init gone and only the SCMI clk at 400 MHz (watch the NoC
  bit-2 de-idle — there was an l5 "0x1ee vs 0x1fa" wrinkle once; confirm genpd alone covers it).
- This makes RK3568 "closer match RK3588" as Jonas put it.

### rockchip,pmu — patch 7 (dts) + patch 6 (binding)
- **Drop `rockchip,pmu = <&pmu>` from the NPU node** — it's for the PMU GRF (pinctrl-style), not
  power-management; power is already expressed via `power-domains`. (Falls out of dropping noc_init,
  its only user.)
- **Drop the binding's RK3568 `rockchip,pmu` requirement** (patch 6).

### Regulator — patch 8
- Drop `regulator-boot-on`.
- Replace the `min=max=1000000` pin with the rail's real description. Open question (below) on the
  exact voltage-management form.

### pmdomain / domain-supply — patch 9
- Keep `need_regulator` + `domain-supply = <&vdd_npu>` (still correct; genpd owns the rail).
- Voltage: see open question.

## How the sashiko v4 items fold in (most are superseded)
(`upstream-prep/sashiko-v4/sashiko-v4-triage.md`)
- **#1 `clk_set_rate` short-circuit** → MOOT (the two-step rate change is removed with PVTPLL).
- **#2 re-apply PVTPLL rate on resume** → MOOT (no PVTPLL; nothing to lose on domain power-off).
  (Was board-CONFIRMED real for the v4 design, but the redesign removes the design.)
- **#3 noc_init returns 0** → likely MOOT (noc_init shrinks/drops).
- **#6 patch-7 CLK_NPU changelog** → rewritten anyway under the new clock approach.
- **#7 dts `rockchip,iommu` v1 comment** → STILL DO (add the comment; unaffected by clock redesign).
- **#4 cores[] bounds-check capacity / #5 fini reset-before-detach** → still optional hardening,
  independent of Jonas; carry if cheap.

## Open question to resolve (with Jonas / Heiko) before v5
**Voltage management without OPP/DVFS.** We run a single fixed compute rate (≤400 MHz) and OPP is
deferred to the future series. So how is `vdd_npu` set to the right operating voltage while
"describing the HW" (no min=max pin, no always-on)?
- Option A: a minimal `operating-points-v2` with one point (400 MHz @ its required mV) + driver
  `dev_pm_opp_set_rate` — but that re-introduces an OPP table Jonas wanted deferred.
- Option B: regulator constraints = the board's real safe NPU-rail range; rely on the boot/default
  voltage being adequate for 400 MHz.
- Need Jonas/Heiko's preference. Likely raise it in the reply or a follow-up.

## Compute gate
Unaffected — still the HW executer-engage mystery (clock now rigorously ruled out). v5 stays an
RFC and keeps disclosing it. Not a blocker for the kernel-side bring-up patches.
