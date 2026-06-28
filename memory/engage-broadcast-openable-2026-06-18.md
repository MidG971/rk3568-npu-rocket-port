---
name: engage-broadcast-openable-2026-06-18
description: gahingwoo's RK3576 engage recipe for the bit16=0 gate — an in-stream BROADCAST OP_EN regcmd entry (target=0x81, reg=0x08, value=0x1d) engages all units; the exact thing to test on RK3568
metadata:
  type: project
---

**🎯 THE ENGAGE LEAD (gahingwoo 2026-06-18) — directly targets our 2-year bit16=0 gate.**
gahingwoo retracted the CBUF-wall framing: on RK3576 conv0 DOES compute (93-distinct feature map),
just **intermittently (~1 in 5-10 runs)** — same regcmd/layout/requant; the rest land 2-distinct
zero-point. An occasional real output ⇒ **layouts + CBUF banking are CORRECT**; "CMAC reads zero from
CBUF" was the CMAC NOT RUNNING that pass, not wrong banks. So the wall = **executers don't engage from
the S_POINTER arming** = our exact "units don't engage, bit16=0 on every unit" symptom.

**THE RECIPE that engages units on RK3576:** an **in-stream BROADCAST OP_EN appended to each task's
regcmd** — ONE entry: **`target=0x81, reg=0x08 (PC_OPERATION_ENABLE), value=0x1d`** — a broadcast that
sets every unit's OP_EN. Without it, every unit stays bit16=0 (configured-but-never-running). As the
rocket {offset,value,routing} inject tuple: **`{0x08, 0x1d, 0x81}`** appended before the final OP_EN.

**▶ EXPERIMENT FOR RK3568 (highest-value test in the project — board on l3):** inject the broadcast
entry `{offset=0x08, value=0x1d, routing=0x81}` into our conv2d regcmd (kernel BSP_INJECT builds the
64-bit word; routing byte = top [63:56]) and check if **bit16 sets** on any/all units (ALL_SP dump /
AFTER_OPEN_ENGAGE). Even an intermittent engage = FIRST TIME bit16 sets on RK3568 = gate broken.
Connects to the long-standing mesa note "broadcast **target=0x81 unsupported on RK3568**" (mesa never
emits it → must inject from kernel). NOTE: verify we haven't already tried this EXACT in-stream
target=0x81 entry (prior "broadcast op_en" tests may have been CPU writes or different routing — the
in-stream 0x81 broadcast bus is the specific mechanism).

## ✅ TESTED ON RK3568 2026-06-18 (l3) — NEGATIVE: recipe does NOT transfer; OP_ENABLE not honored
Added two gated regcmd-append options (`patch_engage.py`, params `engage4`/`bcast_oe`, encoding
low=(val<<16)|off / high=(routing<<24)|0x010000):
- **`engage4`** = 4 per-unit OP_EN appended {0x1008/0x02, 0x3008/0x08, 0x4008/0x10, 0x5008/0x20}=1.
- **`bcast_oe`** = broadcast {0x08, 0x1d, 0x81}.
**Both NEGATIVE:** `AFTER_OPEN_ENGAGE` = CNA=CMAC=CORE=DPU=RDMA=0 (no bit16), `exec_ever=0x0`, out_sum
26214400. **KEY: `ALL_SP` shows `OE008=0` (OPERATION_ENABLE @0x_008) on EVERY unit, while config
`OE00c` LANDS** (CNA=0xa000, CMAC=1, DPU=0x108, RDMA=0x13f) — so the broadcast bus delivers CONFIG to
the units fine, but the OPERATION_ENABLE writes don't latch. PC sits at `TASKST=0xf000` throughout
(programmed AMOUNTS=0x8b constant, not a live counter). ⇒ **gahingwoo's per-unit/broadcast OP_EN recipe
does NOT engage RK3568** — the `0x_008` OP_ENABLE register isn't honored (consistent with the 2-yr
"CNA rejects OP_ENABLE while accepting config" + "OPERATION_ENABLE rejects CPU writes, readback 0").
**This IS the RK3568↔RK3576 divergence gahingwoo wanted: the same OP_EN writes that flip bit16 on
RK3576 are ignored at 0x_008 on RK3568.** Open ambiguity (for him): whether the PC stalls *before*
broadcasting the OP_ENABLE entries vs the register *rejecting* a delivered broadcast — config landing
at 0x_00c but not 0x_008 (adjacent regs, same unit/routing) leans toward register-not-honored, gated
behind a precondition RK3576 has and RK3568 lacks. Board restored to baseline (params off); engage4/
bcast_oe left in (gated off). Reply: `upstream-prep/gahingwoo-engage-test-reply.md`.

**THE CATCH:** reg 0x08 = PC_OPERATION_ENABLE, so the broadcast that engages units ALSO restarts the
PC. Survivable for ONE task (→ conv0's intermittent 93-vs-2 race); in a whole-graph submit
(task_number=N) it restarts the PC every task → PC_TASK_STATUS sits at 0/N, nothing iterates.
**PC_TASK_STATUS offset = 0x48 on RK3576 (NOT 0x3c from the RK3588 header)** — verify OUR RK3568
TASKST read offset (we've reported TASKST=0xf000; confirm it's the right offset).

## ✅ RESET-PRECONDITION LEAD INVESTIGATED 2026-06-19 (l3) — does NOT transfer to RK3568
gahingwoo 2026-06-19: "config-takes-but-0x_008-won't-latch = executer/compute logic held in reset
while register file stays clocked"; on RK3576 a full soft-reset (assert+deassert **SRST_A_RKNN0 +
both CBUF resets SRST_A/H_RKNN_CBUF**) once per power-up before first OP_EN makes OPERATION_ENABLE
latch. **CORRECTIONS he gave:** (1) `OE008` (0x_008 OPERATION_ENABLE reg) = RED HERRING — engaged
RK3576 units ALSO read OE008=0; the real engage witness is **S_POINTER bit16** (engaged=0x1000f). Our
units = 0x0e (no bit16) → genuinely not engaged (conclusion holds, via the right signal). (2) Engage
comes from the **pulse + S_POINTER=0xe arming ALONE** — the in-stream OP_EN (engage4/bcast_oe) was the
WRONG lever (he abandoned it). (3) Dead-end diagnostics: INTERRUPT_RAW_STATUS per-unit bits never fire
even on correct convs (only PC_DONE 0x10000000/0x30000000 worth polling); DRAM coherency is clean
(NPU not dma-coherent → dma_sync does real clean; don't chase stale-DRAM). (4) RK3576 conv-zero =
high-channel-bank TRUNCATION (dump WHOLE output BO; fills ch24-31→ch0-7, cuts short).

**INVESTIGATED on RK3568 → the reset precondition has NO RK3568 equivalent:**
- **RK3568 CRU has NO CBUF or compute-core SRST.** Full NPU SRST set (rk3568 clock header): NIU
  A/H/P (40/41/42), **SRST_A_NPU(43) + SRST_H_NPU(44)**, PVTM(45/46), PVTPLL(47). No RKNN_CBUF, no
  separate core reset. gahingwoo's "deassert the CBUF SRSTs" can't be replicated — they don't exist.
- **BSP (works on RK3568) binds the SAME DT node, SAME 2 resets** (`resets=<&cru SRST_A_NPU>,
  <&cru SRST_H_NPU>`, rk356x-base.dtsi:583) and **`rk356x config .state_init = NULL`** (only rk3576 has
  state_init). So the vendor deasserts no reset rocket misses and runs no state_init on RK3568.
- **rocket ALREADY ports the full BSP `rknpu_soft_reset`** = `rocket_full_reset()` (params `full_reset`
  1=on-resume / 2=per-job, `reset_sim`=TRM 9.5.2 simultaneous srst release): msleep + srst
  assert/deassert + **PVTPLL re-init (opp 600→1000)** + noc_init. TESTED for engage:
  - `full_reset=2 reset_sim=1` (per-job, CONFIRMED running: "FULL_RESET: vendor soft_reset applied"
    + simultaneous srst) → **NO engage** (bit16=0); but the per-job reset WIPES config (CNA
    S_POINTER=0, OE00c=0) so submit degraded; desc_mode=0 didn't fix the wipe.
  - `full_reset=1` (once on-resume) → **wouldn't fire** even after forcing autosuspend
    (delay 1hr→100ms, device→suspended, job→resumed) — no FULL_RESET print; resume path doesn't
    invoke it cleanly. Config stayed clean (0x0e/0xa000), no engage = same as baseline.
- **THE PARADOX (real engage mystery persists):** the BSP ENGAGES on RK3568 using the SAME 2 resets
  rocket uses, state_init NULL, and only soft-resets on error (not normal path) — so on RK3568 engage
  does NOT require the soft-reset at all. And WTRACE proved rocket's CPU writes match the BSP. So
  rocket does the same power_on (SCMI+noc_deidle) + arming(0xe) + pulse as the BSP, yet bit16 never
  sets. ⇒ the RK3568 engage gate is NOT the reset lines / not state_init / not the OP_EN lever — it's
  unexplained, below MMIO visibility, consistent with the long-standing HW-internal conclusion.
- Reply: `upstream-prep/gahingwoo-reset-lead-reply.md`. Board restored baseline (params off, autosuspend
  1hr). Caveat: a PRISTINE "full-reset-once-then-clean-job" engage test is still untested (full_reset=1
  won't fire, full_reset=2 wipes config) — but structural evidence (no CBUF SRST; BSP same resets)
  already says reset isn't our gate.

## 2026-06-19 gahingwoo reframe (RK3576) = CLOCK RACE; our answers = walls DIVERGED
gahingwoo's RK3576 update: ping-pong-into-both-groups ×14 lever combos = CLEAN NEGATIVE (geometry in
consumer group doesn't make MAC compute — matches our PPPARITY). Re-confirmed conv0 regcmd byte-
identical (138/138) + submit matches rknpu_job_subcore_commit_pc reg-for-reg; multi-PD attach (both
PD_NPU0+1) no change. **REFRAME: his wall is a RACE** — identical inferences mostly flat distinct=2 but
occasionally ~150-distinct correct. His one remaining diff: vendor clocks compute engine
(`CLK_RKNN_DSU0`) via **SCMI floating free of the CBUF AXI clock**; mainline routes via `&cru` locked
on the SAME PLL → staging-vs-read phase gamble. Fix he's pursuing: reparent compute clk to a different
PLL / SCMI.
**OUR ANSWERS (board clk_summary 2026-06-19):**
1. **RK3568 compute vs CBUF/AXI clocks ARE decoupled:** compute `clk_scmi_npu`=1GHz SCMI/PVTPLL (no cru
   parent); CBUF/AXI `aclk_npu`=200MHz on GPLL (gpll→clk_npu_src→clk_npu→aclk_npu). Different source
   AND rate → already the vendor-like decoupling RK3576 lacks. Yet we still wall ⇒ per gahingwoo's own
   logic this WEAKENS the clock-race theory (caveat: we wall at engage, never reach the compute race,
   so can't directly test it).
2. **RK3568 = DETERMINISTIC hard-zero** (out_sum 26214400 EVERY run, distinct=2, never an occasional
   correct) because **bit16 never sets (never engage).** ⇒ NOT a race. **Walls DIVERGED:** gahingwoo =
   engages-but-compute-races (post-engage timing); rocket = never-engages (static, upstream). Our
   determinism is itself a narrowing: our gate is a STATIC config/state diff, not a timing window.
gahingwoo offered a sweep harness (sets knobs from boot script, prints distinct/combo to serial) —
less applicable to us (deterministic, not a race). Reply: `upstream-prep/gahingwoo-clockrace-reply.md`.

## 2026-06-19b — gahingwoo's set_rate/GRF-PVTPLL off-NPU lead RULED OUT (with data)
gahingwoo reverse-engineered our RK3568 BL31 SCMI NPU clock: `clk_scmi_npu_set_status()` (enable) =
no-op; `clk_scmi_npu_set_rate()`→`clk_npu_set_rate()` programs CRU CLK_SEL(7) NPU div+mux AND a
**GRF write at 0x740 (RK3568_NPU_PVTPLL_CON0, OUTSIDE the NPU block) — PVTPLL ring length + mux
NPU→PVTPLL**, in firmware, invisible to an NPU-reg trace. Lead: if rocket only clk_bulk_enables, none
of it runs. Caveat (from his RK3576): set_rate can SILENTLY no-op (SCMI `quirk_clock_rates_triplet_out_
of_spec`) → must read rate back.
**CHECKED on RK3568 = lead SATISFIED, not our gate:**
- **NO SCMI quirk on RK3568** (no quirk_clock log) → set_rate not subject to the silent no-op.
- **rocket DOES set_rate scmi_clk** (devm_pm_opp_set_clkname "scmi_clk" + dev_pm_opp_set_rate 600→1000,
  rocket_core.c:88/275-276), and it **LANDS: `clk_scmi_npu` reads 1000000000 (1GHz)**, not 0/default.
- **GRF 0x740 = 0x000000d3** (programmed, non-default) → the firmware PVTPLL GRF write DID happen.
  (0x744/748/74c = 0.) rocket+BSP both reach 1GHz via the SAME BL31 clk_npu_set_rate → identical GRF.
⇒ the off-NPU clock poke gahingwoo flagged ALREADY happens on rocket; compute clock is up @1GHz on
PVTPLL, programmed + decoupled. **NOT our engage gate.** (gahingwoo also noted RK3576 engaged on plain
GPLL w/o PVTPLL, so PVTPLL isn't the arming key anyway.)

## 2026-06-20/21 — gahingwoo CLOSES the clock road (RK3576); RK3568 clock confirmed CLEAN
gahingwoo's SCMI no-op root cause = **clock-ID MISMATCH**: mainline `CLK_RKNN_DSU0=232` vs RK3576 TF-A
`clock_table` key **238** → `<&scmi_clk 232>` hit an empty slot (set_rate no-op, get_rate 0); `<&scmi_clk
238>` is settable. But fixing it = DEAD END: on PVTPLL (right index/rate, OPP voltage first, set_rate
after soft-reset) the NPU completes **0 jobs** (~83 drm_sched timeouts/90s) — PVTPLL needs the WHOLE
vendor DVFS stack (per-chip nvmem leakage cal + OPP/devfreq governor trimming the ring oscillator), not
just set_rate. Reverted to GPLL. He's **at the end of the clock road**, writing up the "every surface
matches vendor, conv0 still zero" map for upstream.
**RK3568 check (his ask) = BOTH failure modes RULED OUT (data):**
- **No ID mismatch:** NPU node binds `<&scmi_clk SCMI_CLK_NPU>` (=2, named mainline constant); live
  `clk_scmi_npu` reads back **1GHz** (a mismatch→empty-slot would read 0, like his 232). Index matches
  what our TF-A keys clk_scmi_npu at → set_rate LANDS.
- **Clock is USABLE:** GRF 0x740=0xd3 (PVTPLL programmed), and the NPU RUNS on it (PC advances, IOMMU
  AXI moves; BSP computes CORRECTLY on this identical clock). Unlike RK3576, RK3568's SCMI/PVTPLL is a
  working clock with the DVFS already handled in TF-A.
⇒ gahingwoo's entire clock road (his last big SoC-difference) is a NON-ISSUE for RK3568 — clock is
right-index + settable + usable + decoupled. STRENGTHENS our localization: the gate is NOT the clock.
Our gate remains the PC-driving-armed-units-0→0x05 step ([[bsp-engage-headarm-2026-06-19]]). Both
projects converging on the same upstream conclusion: every observable surface matches the vendor, NPU
still won't compute (he: zero output post-engage; us: never engages). Reply:
`upstream-prep/gahingwoo-clockroad-close-reply.md`.

**▶ ENGAGE-GATE HYPOTHESES NOW EXHAUSTED (all ruled out with data):** regcmd content (byte-identical),
CPU NPU-register writes (WTRACE match), reset lines (no CBUF/core SRST; BSP same 2), state_init (NULL),
OP_EN lever (per-unit+broadcast), clock-coupling (decoupled; deterministic not race), power domains
(multi-PD), NoC de-idle (matches), set_rate/GRF-PVTPLL (lands, GRF programmed). Everything the vendor
does that we can observe, rocket does — yet rocket deterministically never sets bit16. Gate is below
trace visibility / genuine RK3568 rocket-vs-BSP difference (possibly probe-time ORDER/timing not
captured by static write traces, or genuine HW state). Reply: `upstream-prep/gahingwoo-setrate-grf-reply.md`.

## 2026-06-19c — ORDERED PROBE-TIME CAPTURE (l1 BSP vs l3 rocket): steady-state HW IDENTICAL
Boot-switched to l1 (BSP RKNPU, built-in) and back, serial-attended (logs/serial/probeorder-*.log).
ftrace NOT built in either kernel (no tracers/filter-funcs/clk-regmap events) → used drivers' own logs.
- **rocket probe order (l3 reload):** opp_set_rate(1GHz) → noc_deidle (PWR=0x7d) → PP_INIT
  (0x1004=0xe, 0x1024=0x80000000) → iommu → PROBE_CNA(S_POINTER=0xe). 
- **BSP probe order (l1 boot dmesg):** iommu-mode → init → noc_deidle (NOC_AUTO=0xfffb) → vdd 1V →
  devfreq 1GHz. **Same broad sequence** (power/clk up → 1GHz/1V → noc_deidle → iommu); regulator is
  always-on (sram-supply=npu-supply=vdd_npu, always-on) so regulator-vs-clk order is moot.
- **⭐ STEADY-STATE HW BYTE-IDENTICAL (l1 BSP == l3 rocket), via /dev/mem syscon reads:**
  GRF 0x740 PVTPLL CON0 = **0xd3** (both); PMU PWR_STATUS 0x98 = **0x7d** (both); PMU NOC_AUTO 0x70 =
  **0xfffb** (both); PMU BUS_IDLE_ST 0x68 = **0x1ea** (both). The boot-dmesg diffs (BSP PWR=0xff,
  BUS_IDLE=0x1fa) were TRANSIENTS during the de-idle, not steady-state.
- **⇒ ORDERING + all power/clock/PVTPLL/NoC/regulator state CONCLUSIVELY RULED OUT.** Every observable
  register state (power domain, NoC, PVTPLL, clocks, IOMMU, NPU config) is IDENTICAL between the
  WORKING BSP and broken rocket; probe orders equivalent; submit writes match (WTRACE); regcmd
  byte-identical. Yet BSP engages, rocket doesn't, deterministically. **The engage gate is below ALL
  observable register state** — exhaustive re-confirmation of the 2-yr "HW-internal" conclusion.
- Reset/regulator/SRAM busted too: RK3568 sram-supply=vdd_npu (single always-on rail), no separate
  SRAM regulator (the BSP `rknpu_dev->mem` enable is RK3588-class / no-op here). Board restored l3.
  Reply: `upstream-prep/gahingwoo-probeorder-reply.md`.

**THE DEEPER OPEN Q:** the vendor regcmd has NO in-stream OP_EN — 139 pure-config entries; units
engage from `S_POINTER=0x0e` arming + ONE `PC_OP_EN=0x1` pulse, and the PC iterates all N tasks.
**Why does the vendor's arming engage the executers and rocket's doesn't?** A sequence/MODE difference,
not a register value (byte-identical). The broadcast OP_EN is a workaround; the real fix is matching
the vendor's arming sequence. A RK3568-vs-RK3576 engage comparison localizes it once either side gets
bit16 to set.

**CBUF banking (lead UPDATED):** RK3576 does NOT use the RK3588 12-bank WEIGHT_BANK/DATA_BANK formula
— `CBUF_CON0 (0x1040)` = vendor-captured CONSTANT `0x10000000` (first-conv) / `0x14000000` (in_ch>4),
NOT a computed `0x44` split; `CBUF_CON1 (0x1044)` = `(in_w<<16)|surf`, not entries-per-bank. So my
"12-bank wrong" lead is already bypassed on RK3576 — BUT on **RK3568 the legacy RK3588 encoder path
still computes the 0x44-style split**, so forcing the captured constants (0x10000000 / 0x14000000) is a
clean A/B on RK3568 (DOWNSTREAM of engage). See [[gahingwoo-cbuf-iommu-wall-2026-06-17]],
[[firstconv-weight-packing-bug-2026-06-17]], [[ppgroup-parity-reframe-2026-06-17]].
