# ROCK 3B NPU Project Memory

## 🎉🎉 ENGAGE GATE SOLVED 2026-06-24 (the 2-year blocker) — READ FIRST
- [BREAKTHROUGH: BSP-primer engage](BREAKTHROUGH-engage-bsp-primer-2026-06-24.md) — **BSP-primer (rknpu prime→rmmod→rocket, no power cycle) makes rocket's executers ENGAGE: all 5 units S_POINTER=0x1000e (bit16), exec_ever=0xf, config lands — byte-matches working rknn. vs every cold run bit16=0.** ⇒ **engage gate = one-time VENDOR-ESTABLISHED power-up/init state** (not regcmd[Method B], not register write, not IOMMU/clock). Completion still fails (TASKST=0xf000, dt_wr=0) = next wall (gahingwoo's coeff buffer). **▶ NEXT: probe-only-vs-job prime test (fix path); find the specific vendor init to port → rocket engages w/o prime.** Setup: l8 (Image-6.19-primer, rknpu+rocket=m), `upstream-prep/bsp-primer-setup.md`.

## 🎯 REFRAME (2026-06-17, read first)
- [PP-group parity reframe](ppgroup-parity-reframe-2026-06-17.md) — gahingwoo reframe: RK3568 non-engage may be CORRECT gating on an empty/misaligned PP group. **MEASURED 2026-06-17 (l3): NEGATIVE — ds0_g0==ds0_g1==0x0280002c (geometry in BOTH banks incl. consumer group), exec16=0, no engage ⇒ parity is NOT our gate; stuck deeper/upstream of RK3576.** bit4 PP_CLEAR absent on 3568 (0x1e→0xe). Attach-once validated by gahingwoo.
- [gahingwoo CBUF/iommu wall](gahingwoo-cbuf-iommu-wall-2026-06-17.md) — gahingwoo RK3576 post-engage (REFINED 2nd reply): output GENUINELY ~0, gate = CBUF execution STATE not regcmd. **Parity theory WALKED BACK (corroborates our PPPARITY): warm-state all 4 units engage, geometry latches.** Data path PROVEN complete (16-byte counter scale off known 1536B weight buf: feature/weight/output all full-size) yet distinct=2 → CMAC reads ZERO from CBUF. **⚠️ METHODOLOGY: counter clear doesn't latch in re-kick loop (cumulative) — trust output `distinct`, not counter deltas.** RED HERRING: `core wt_rd=0`/`FDA=0` NORMAL; real read=`top wt_rd`. CBUF-init = vendor full soft-reset + **on-chip SRAM partition setup**; blocker = mainline rk_iommu can't survive raw MMU reset post-NPU-reset (`MMU_DTE_ADDR not functioning`) → **rk_iommu reset-survival = shared next target (our wheelhouse; gave "disable fetch dte time limit" lead)**. Our engage gate is EARLIER+distinct; possible unification = soft-reset+SRAM-init may BE our engage trigger.
- [firstconv weight-packing bug](firstconv-weight-packing-bug-2026-06-17.md) — **🎯 gahingwoo 3rd update: RK3576 conv0 zero-output = MESA weight-packing bug** (firstconv takes generic RK3588 packing → 18432B buf, but CNA DMA 0x101c reads 1536B firstconv-ARGB layout → wrong bytes/order → MAC≈0 → zero-point). Soft-reset theory DEAD (didn't fix compute); OUT_CVT sweep flat. **IOMMU reset-survival = SEQUENCING (reset-before-attach), validates our attach-once/reset-before-detach — NOT my DTE-patch lead.** **MEASURED RK3568 2026-06-17:** firstconv-ARGB SIZE bug RULED OUT (mesa generic packing, sizes correct: 51200=5·5·16·128). **✅ but byte-ORDER (TILING) bug CONFIRMED via mesa-runtime-buf vs `conv2d-bsp.rknn`** (`for_review/weight-packing-analysis-2026-06-17.md` + `mesa-weights-512.log`): same weights (mesa[0:256] byte-identical to vendor @0x1bc0) but mesa packs CONTIGUOUS while RK3568 vendor INTERLEAVES 256-B atoms with 6144-B/24-atom period → CMAC reads scrambled weights → wrong MAC. **Pure mesa fix (rkt_ml.c tiling); #1 output suspect WHEN we engage.** Vendor data banked via .rknn (no l1 boot needed). STILL downstream: uniform-128 now = NON-engage, engage gate is THE blocker.

## 🏁 METHOD B CONCLUSIVE 2026-06-22 — ENGAGE GATE = ROCKET KERNEL DRIVER (read first)
- [Tomeu/Method B](tomeu-engaged-payload-replay-2026-06-22.md) STEP 3 DONE. **Replayed the VENDOR's byte-exact working conv2d cmdstream (computes distinct=24 via rknn) through the ROCKET kernel UABI (`rocket_replay.c`) → SAME stall: TASKST=0xf000, no engage (bit16=0 all units), timeout, distinct=1. Both desc_mode 0/1.** Same bytes work via rknn, fail via rocket ⇒ **gate isolated to the ROCKET KERNEL DRIVER** (mesa/cmdstream/coefs/input/descriptor ALL eliminated). Decisive after 2yr. **NEXT: what in rocket's kernel submit differs from rknn given identical cmdstream + matched PC-writes/power/clock + no rk3568 init NPU writes — suspects: IOMMU domain/mapping mechanics, writel order/timing, BO residency. Harness `rocket_replay` now lets us A/B arbitrary payloads + kernel tweaks vs the rknn golden.** REPORT-WORTHY to Tomeu+gahingwoo. **DRILLING: ruled out descriptor-mode + CPU-arm. 🚨 found CRITICAL CONFOUND — `OP_EN_PATCH` (rocket_job.c:543, gated only by attached_domain=always) UNCONDITIONALLY rewrote regcmd in EVERY job w/ stale yolov5s values → many 2-yr "config landed" obs were this mangling (added `replay_raw` param to bypass, patch_replayraw.py). CLEAN replay (replay_raw=1) STILL stalls → gate=rocket kernel HOLDS. Clean detail: PC fetches regcmd but CONV_CON1=0/wt_rd=0 → units never get config broadcast. TASK_CON SETTLED (identical formula, ruled out). GOLDEN CONTRAST captured: rknn S_POINTER=0x1000e/S_STATUS=0x0c (ENGAGED) vs rocket 0x0e/0 (not), SAME cmdstream. **✅ CLK_GATE RETEST DONE 2026-06-22b = NEGATIVE (ruled out).** rocket CLK_GATE(0x1090)=0x00000000; rknn CLK_GATE=0x00000000 (captured via existing DUMP_BEFORE_OPEN, which already reads 0x1090 — NO rebuild, NO /dev/mem). **Same value (0) on both → not the differentiator.** ⚠️ LESSON (power-cycle cost): /dev/mem NPU-MMIO read is OK on rocket/l3 but HANGS the board on rknn/BSP l1 (NoC deadlock, no panic→power cycle) — use KERNEL dumps for rknn regs, never /dev/mem on l1.
**🎯 REFINED PICTURE (key):** rknn & rocket start from BYTE-IDENTICAL CNA pre-state (BEFORE_OPEN: S_STATUS=0, S_POINTER=0, CBUF_CON0=0x44, CLK_GATE=0), get the SAME cmdstream → rknn AFTER_OPEN S_POINTER=0x0001000e (bit16 ENGAGED, S_STATUS=0x0c); rocket stays 0x0e (no bit16). **CONV_CON1=0 is NON-diagnostic (rknn shows it too at AFTER_OPEN while engaged+computing) — ONLY S_POINTER bit16 is the engage signal.** So every observable input is identical and only the HW bit16 differs. **✅ IOMMU/RESIDENCY DRILL DONE 2026-06-22b = WEAK suspect / effectively ruled out for ENGAGE.** Reasoning + evidence: (1) **extra-CPU-writes ALREADY OFF** — the PROBE/MYSTERY_INIT/DPU_BSP dmesg lines are READBACKS, the writes are commented `// CPU_PREWRITES_OFF` (ruled out). (2) **Engage (bit16) is NPU-INTERNAL PC↔unit OP_EN handshake that happens BEFORE memory access** → IOMMU/residency gate DATA access AFTER engage, not engage itself → weak suspect. (3) **rocket's units never start reading DRAM** (clean replay PERF dt_rd=0x89 = just PC's ~2KB regcmd fetch, no unit input read; wt_rd=0) → failure is UPSTREAM of memory access. (4) post-job /dev/mem reads all reset to 0 (not diagnostic; units idle post-timeout). **CONVERGED CONCLUSION: engage gate = NPU-internal PC↔unit OP_EN handshake not completing on rocket, with EVERY observable input matched to working rknn (pre-state S_STATUS0/S_POINTER0/CBUF_CON0 0x44/CLK_GATE0, byte-exact cmdstream, PC writes TASK_CON/AMOUNTS/INT_MASK/OP_EN/slave-prewrite, extra-writes off, desc/arm). Below MMIO visibility = genuinely HW-internal OR submit-env subtlety.** ▶ ONLY remaining avenue = Tomeu's FULL module-load→submit MMIO trace INCLUDING IOMMU/CRU/PMU regs + SEQUENCE (NPU regs showed no init writes; a non-NPU/IOMMU-enable sequence/timing diff wouldn't show as steady-state) — needs l7 trace kernel (clk/regmap tracepoints). **This is the complete package — REPORT to Tomeu/gahingwoo.** ✅ `methodB-report-gahingwoo.md` POSTED to issue #1 2026-06-22 (public, confound removed). **▶ TODO: send `methodB-report.md` (TOMEU version, KEEPS confound) to Tomeu (email IRC thread + Mesa MR 42134).** ▶ AWAITING gahingwoo's interpretation (what completes the OP_EN→engage handshake on rk3576). **✅ L7 CAPTURE DONE 2026-06-23:** (1) **boot IOMMU attach IDENTICAL** rknn-vs-rocket: both `rk_iommu_attach_device fde40000.npu type=3 dt_dma=0x493000` at probe (~2.45s) → IOMMU init order NOT the diff (another suspect ruled out). (2) rocket adds a per-job detach+attach (its own domain) that rknn doesn't — the ONLY IOMMU difference, but downstream of engage + attach-once already validated = weak. (3) ⚠️ **clk/regmap tracepoints ABSENT in the l7 trace kernel (NO_TRACEPOINTS)** → the CRU/PMU/GRF init *sequence* (Tomeu's "non-NPU init") is NOT tracepoint-capturable; needs either a kernel rebuilt with CONFIG tracing events OR a source-level order diff (rknpu_power_on vs rocket runtime_resume — partly done). (4) rknn boot NPU WTRACE = 0 writes (confirms no rk3568 init NPU reg writes). Artifacts: `for_review/l7-boot-init.log`, `wtrace-bsp-20260623-123441.npu-submit.log`. ▶ Remaining init-diff avenue = clk/PMU/reset ORDER (source-level, since not tracepoint-capturable). **✅ SOURCE-LEVEL ORDER DIFF DONE 2026-06-23 → found a real divergence: IOMMU `trigger_enable` TIMING.** BSP `rknpu_power_on` (once/resume, BEFORE job): regulator(vdd)→regulator(mem)→clk_bulk_enable→SCMI 2-step→noc_deidle→**rk_iommu_trigger_enable**→pm_runtime_get; then job-time commit_pc does NO iommu ops (domain attached at probe, stays). **rocket SPLITS it: runtime_resume = clk→opp_set_rate→noc_init (NO trigger_enable); then job_run = iommu_detach→iommu_attach→rk_iommu_trigger_enable→flush_tlb→hw_submit(OP_EN), ALL at job-time right before OP_EN.** So rocket re-enables/re-attaches the IOMMU IMMEDIATELY before OP_EN every job; BSP enables it at power-on, settled well before submit. This TIMING diff wouldn't show as a steady-state reg (survived all reg comparisons) + matches Tomeu's "NPU must be in known-working state by submit time". **▶ NEXT EXPERIMENT (testable via harness, l3 replay_raw=1): move rocket's IOMMU attach+trigger_enable to resume-time (BSP-style, attach-once early) so the IOMMU is settled before hw_submit, NOT re-touched at job time → re-check engage (S_POINTER bit16).** (clk/PMU order otherwise similar; sram `mem` regulator moot = sram-supply=vdd_npu always-on.) **✅ TESTED 2026-06-23 (iommu_settle_ms param, patch_iommu_settle.py): added msleep(50) between rocket's trigger_enable/flush_tlb and hw_submit → STILL STALLS (no engage, TASKST=0xf000). IOMMU-settle TIMING ruled out** (50ms huge for settling; + boot-attach identical per L7; + PC regcmd fetch already works = translation fine). **⇒ IOMMU/residency THOROUGHLY EXPLORED, NEGATIVE.** Remaining structural IOMMU diff (rocket switches boot-default→rocket-custom domain at job time vs BSP one-domain) not cleanly testable for replay (fresh fd=new domain each run) + engage is pre-memory → very weak. **Gate stands: NPU-internal PC↔unit OP_EN handshake, ALL observable inputs + IOMMU + init-timing matched/ruled-out — below MMIO visibility.** Params neutralized via sysfs (NO rmmod after replay = hang hazard); board l3, rocket loaded, accel0 ok. **▶ IOMMU follow-up comment DRAFTED `upstream-prep/gahingwoo-iommu-followup.md` (NOT posted) to append to gahingwoo issue #1 after the Method B report.** Next experiment = L7 init/IOMMU-sequence capture+diff [PARTIALLY DONE: IOMMU matched, clk-seq blocked]. ✅ L7 SET UP + capture plan finalized `upstream-prep/wtrace-trace-setup.md` (L7 READY block: boot l7 → capture-wtrace.sh + dmesg rk_iommu grep → init/IOMMU-sequence diff; default left l3). ⚠️ **HAZARD LESSON: `rmmod rocket` HANGS the system after a rocket_replay (the timed-out/PREP_BO-EBUSY job wedges the drm_sched; rmmod blocks → unrecoverable → reboot).** After a replay, DON'T rmmod — reboot, or just set params via sysfs / leave loaded. Board recovered (rebooted to l3, rocket healthy version:0) 2026-06-22b.
**(orig) CLEAN CLK_GATE retest** — does un-gating CNA sub-blocks (CLK_GATE 0x1090: CBUF/WEIGHT/CSC/FEATURE) let the PC broadcast land (CONV_CON1≠0) + units engage? Must be replay_raw=1 (avoid OP_EN_PATCH confound). Mechanism Q: 0x1090 was CPU-write-rejected in old (confounded) tests → may need to inject as a regcmd broadcast entry. Board on l3 baseline, Tailscale .58. ALL replay/state tests REQUIRE replay_raw=1.

## 🧩 RK3576 #2-GATE SPEC (gahingwoo 2026-06-23) — reusable for OUR post-engage stage
- [gahingwoo SDP coeff buffer](gahingwoo-sdp-coeff-buffer-2026-06-23.md) — decoded RK3576 post-engage saturation = **SDP requant coefficient buffer**: A=int32[128] bias (`0x80*(weight_sum+bias)`, driver already right), B=int32 scalar (`0x80−wt_zp`), + a **float surface = 2nd copy of dequant weights (kernel-innermost) for the rescale stage that the open driver leaves ZEROED** → saturation (proven load-bearing). Engine reads weights TWICE (int8→MAC, float32→rescale). **Our #2 gate after engage** (separate from int8 tiling [[firstconv-weight-packing-bug-2026-06-17]]). Shared-wall framing: 2 SoCs, 2 register-invisible sub-stages (ours=engage handshake, his=coeff delivery). Ack reply drafted `upstream-prep/gahingwoo-sdp-ack-reply.md`.

## 🧭 TOMEU (rocket author) ENGAGED 2026-06-22 + key references
- [Tomeu engaged / payload-replay](tomeu-engaged-payload-replay-2026-06-22.md) — **Tomeu Vizoso joined gahingwoo issue #1.** Method B (decisive): replay hardcoded rknpu2 payload {input,coefs,cmdstream,output} through BOTH rknn(works) + rocket UABIs → if rocket fails on identical bytes, gate isolated to rocket KERNEL driver (mesa out). We've done a partial (byte-exact BSP regcmd→rocket, no engage) leaning that way. **NB Tomeu's "pipeline runs end-to-end, congrats" = gahingwoo's RK3576 stage (PC completes), NOT us (PC stalls 0xf000/times out — a stage earlier).** Multi-task tiling = N/A for us (conv2d single-task, fits 256KB CBUF). CBUF_CON0 split already swept NEG. flatc-simplify (wt=1/bias=0) = gated behind engage.
- [allbilly/rk3588 reference](allbilly-rk3588-reference.md) — pure-Python register-level conv on same NVDLA engine; CBUF DATA_BANK/WEIGHT_BANK split spelled out (debug_conv.md). CBUF_CON0 fields: DATA_BANK[3:0]/WEIGHT_BANK[7:4]/FC_DATA_BANK[10:8]/DATA_REUSE[12]/WEIGHT_REUSE[13]. Untried bit = weight-load register-ORDER in the .py.

## 🎯 BSP ENGAGE REFERENCE + HEAD-ARM LEAD 2026-06-19 (read first for engage gate)
- [BSP engage / head-arm](bsp-engage-headarm-2026-06-19.md) — captured WORKING BSP yolov5s engage on l1: all units S_STATUS=0x05, CORE S_POINTER=0x1000e (bit16), PC TASKST=0x11000, perf dt_wr/dt_rd/wt_rd growing. **BSP regcmd HEAD arms all 5 S_POINTER=0xe via broadcast (0x1004/0x2004/0x3004/0x4004/0x5004); mesa OMITS CNA/CMAC/CORE (0x1004/0x2004/0x3004).** rocket's CPU pre-arm (pp_init) ≠ broadcast-at-head. **TESTED 2026-06-20 (`head_arm` param, `patch_headarm.py`): arming LANDS (CNA/ACCU S_POINTER=0x0e via broadcast) but NO engage — units stay S_STATUS=0 (not 0x05), PC TASKST=0xf000. ⇒ head-arming NOT the gate.** REFINED GATE: rocket arms units (S_POINTER=0xe by every method) but **PC never drives them 0→S_STATUS=0x05** (PC stalls 0xf000; BSP PC→0x11000 DONE, units→0x05+perf). The PC-driving-units-active step is missing — HW-internal/submission-infra, NOT regcmd-content/arming/power/clock (all matched; byte-exact vendor regcmd replay also fails). Ref: `for_review/bsp-regcmd-yolov5s-20260619-153839.txt`.

## 🏁 WTRACE DIFF DONE 2026-06-17 — CPU-side submit IDENTICAL vendor-vs-rocket; gate NOT in kernel
- `upstream-prep/wtrace-trace-setup.md` (FINAL VERDICT block). BSP l7 captured by user (`wtrace-bsp-20260617-100509.{npu-boot,npu-submit}.log`), diffed vs `for_review/rocket-wtrace-20260617-095509.log` via `upstream-prep/wtrace-diff.py`. **BSP submit NPU writes = 8 offsets {PC 0x8/0x10/0x14/0x20/0x24/0x30/0x34 + 0x8010 perf}, NO sub-unit CPU writes — rocket MATCHES all** (0x8010 was false-positive: rocket writes it via direct writel ~rocket_job.c:1788, trace only hooked the 3 macros). Deltas = model-dependent (AMOUNTS/IOVA/task#) or benign (0x34=0). BSP npu-boot = ZERO writes (no probe-time NPU writes). **(B) power layer: rocket `rk3568_noc_init`==`rknpu_noc_deidle` (PMU 0xa0/0x70/0x50 + BUS_IDLE poll); live state healthy (BUS_IDLE_ST=0x1ea bit2=0, PWR=0x7d NPU on, IOMMU ST=0x19 no fault) yet TASKST=0xf000.** ⇒ **NO missing CPU-side init: closes gahingwoo "missing probe/reset init" (none exists) + validates RFC v3 (kernel correct); gate = regcmd broadcast payload (mesa) or HW-internal/IP, NOT the kernel driver.** Tooling: `patch_wtrace.py` (gated `rocket_wtrace` param), `wtrace-diff.py`. Caveat: NPU-reg trace must cover direct writel too, not just macros. See [[ppgroup-parity-reframe-2026-06-17]].
- **▶ TWO REPLIES POSTED 2026-06-17** (issue #1): (a) `gahingwoo-ppparity-reply.md` = parity-negative + WTRACE verdict (CPU-side identical, no missing init); (b) `gahingwoo-cbuf-iommu-reply.md` (LIVE, supersedes) = acked his CBUF-state wall + gave rk_iommu lead (merged "disable fetch dte time limit"), re-asked the engage delta. weightpacking-reply POSTED 2026-06-17. **gahingwoo 2026-06-18 (REFINED-3):** retracted "first-conv computes" — **every conv computes MAC=0** (standalone in_ch=16 engages+DPU-writes but 2-distinct; requant 2¹⁷ sweep byte-identical + soft-reset ruled OUT). Wall = **CNA→CBUF→CMAC: CNA reads full DRAM, DPU writes full output, but CMAC reads ZERO from on-chip CBUF** (no readable reg). Decoded reusable layouts: input=tight NHWC `buf[(h·W+w)·3+c]` raw no-pad; pointwise wt=`buf[oc·ic+ic]`. - [engage broadcast/per-unit OP_EN](engage-broadcast-openable-2026-06-18.md) — **🎯 gahingwoo's RK3576 engage recipe (in-stream per-unit OP_EN {0x1008/0x3008/0x4008/0x5008}=1 or broadcast {0x08,0x1d,0x81}) TESTED on RK3568 2026-06-18 = NEGATIVE.** bit16=0 all units, **OE008(OPERATION_ENABLE)=0 while config OE00c LANDS** → RK3568's 0x_008 OP_ENABLE not honored (2-yr wall confirmed); same writes engage RK3576. This IS the RK3568↔RK3576 divergence. NEW ask to him: what precondition makes RK3576 HONOR the OP_ENABLE write. Params `engage4`/`bcast_oe` (`patch_engage.py`) left gated-off. Decisive next test: place OP_ENABLE BEFORE the ~126 PC-stall to tell PC-stall-before vs register-rejects.

reply `upstream-prep/gahingwoo-cbuf-staging-reply.md` ✅ POSTED 2026-06-18: offered **CBUF-banking lead** (CBUF_CON0 0x1040 split/CBUF_CON1 computed for RK3588 12 banks vs RK3568 8 / RK3576≠12 = textbook "CMAC reads zero from CBUF"). engage-test reply POSTED 2026-06-18; gahingwoo replied 2026-06-19 with the RESET precondition (full soft-reset incl. CBUF SRSTs makes OP_ENABLE latch) + corrections (OE008 red-herring→S_POINTER bit16; pulse+arming not in-stream OP_EN; RAW-per-unit & stale-DRAM dead; conv-zero=channel-bank truncation). **INVESTIGATED on RK3568 = lead does NOT transfer** ([[engage-broadcast-openable-2026-06-18]] RESET block): RK3568 has NO CBUF/core SRST (only A/H_NPU+NIU+PVTM/PVTPLL); BSP works w/ SAME 2 resets + state_init NULL + soft-resets only on error; rocket full_reset port (PVTPLL re-init+sim srst) → no engage. PARADOX: BSP engages w/ same resets+writes (WTRACE-confirmed) yet rocket doesn't → gate not reset/not NPU-reg. reset-lead reply POSTED 2026-06-19. **gahingwoo 2026-06-19 follow-up: RK3576 wall reframed = CLOCK RACE** (ping-pong-both-groups ×14 = clean neg; regcmd 138/138 + commit_pc match; multi-PD no change; occasional ~150-distinct correct = timing window; suspect compute clk `CLK_RKNN_DSU0` vs CBUF AXI clk on SAME PLL via &cru, vendor floats compute via SCMI). **Our answers (clk_summary): RK3568 compute clk (scmi_npu 1GHz SCMI/PVTPLL) ALREADY decoupled from CBUF/AXI (aclk_npu 200MHz GPLL); our failure DETERMINISTIC (never engage) → WALLS DIVERGED (him=engages-but-races, us=never-engages-static).** gahingwoo 2026-06-19b: reverse-engineered our BL31 — SCMI NPU clock's CRU mux + **GRF 0x740 PVTPLL write** happen only on `set_rate` (off-NPU, off-trace); asked if rocket only enables vs set_rates. **CHECKED = RULED OUT (data): rocket set_rates scmi_clk→1GHz (no SCMI quirk on RK3568), GRF 0x740=0xd3 (PVTPLL programmed)** → off-NPU clock poke already happens ([[engage-broadcast-openable-2026-06-18]] 2026-06-19b). **⇒ ENGAGE HYPOTHESES EXHAUSTED** (regcmd/CPU-writes/reset/state_init/OP_EN/clocks/power/NoC all ruled out w/ data; deterministic→static gate, below trace). **▶ reply `upstream-prep/gahingwoo-engage-driving-reply.md` ✅ POSTED 2026-06-20** (consolidated ordering-ruled-out + BSP engage reference (S_STATUS=0x05/CORE 0x1000e/TASKST=0x11000/perf) + head-arm test (lands, no engage)). gahingwoo 2026-06-20/21 CLOSED the clock road (RK3576 SCMI no-op = clock-ID mismatch 232 vs TF-A 238; fixed but PVTPLL needs full nvmem-leakage+devfreq DVFS stack → 0 jobs; reverted GPLL; writing upstream "every surface matches, conv0 zero" map). **RK3568 checked = clock CLEAN both ways: index matches (clk_scmi_npu reads 1GHz not 0), usable (GRF 0x740=0xd3, NPU runs on it). Clock NOT our gate — strengthens localization.** LIVE reply DRAFTED `upstream-prep/gahingwoo-clockroad-close-reply.md` (NOT posted; offers RK3568 half of joint upstream map). **gahingwoo 2026-06-21 posted FINDINGS.md** (mirror `for_review/gahingwoo-FINDINGS-rk3576-2026-06-21.md`): submit-time race RULED OUT (busy-wait sweep, 0/1000 vary) → gate locked PRE-JOB in CBUF/power-up state; RK3576 bug = CNA-weight→CMAC weight-read handoff latch (core wt_rd=0 despite weights staged); asked "where are you" + offered to MIRROR reset/power-up experiments. **reply `upstream-prep/gahingwoo-findings-reframe-reply.md` ✅ POSTED 2026-06-21** (subsumes the clock-clean conclusion from the unposted clockroad-close draft): restated our engage status (every surface matched, gate=PC won't drive armed units 0→0x05), AGREED pre-job reset/power-up class, **PROPOSED the BSP-PRIMER swap** (rknpu prime→rmmod→modprobe rocket no power cycle = clean pre-job-state discriminator; he can mirror; our wrinkle = need both as modules on one boot/kernel build), ASKED whether WORKING vendor shows core wt_rd≠0 (resolves 06-17 red-herring vs 06-21 bug tension). **▶ AWAITING gahingwoo: (1) BSP-primer mirror result on RK3576; (2) core wt_rd≠0 on working vendor?** See [[gahingwoo-cbuf-iommu-wall-2026-06-17]] REFINED-FINAL, [[bsp-engage-headarm-2026-06-19]]. **▶ OUR next board task: build rknpu+rocket as co-loadable modules → run BSP-primer swap** (sharpest pre-job-state test). Other open thread: same-conv regcmd byte-diff (conv2d vs conv2d-bsp.rknn).
---
setrate-grf reply POSTED 2026-06-19. **ORDERED PROBE-TIME CAPTURE DONE 2026-06-19c (l1 BSP vs l3 rocket, serial-attended boot-switch): ORDERING RULED OUT — steady-state HW BYTE-IDENTICAL** (GRF 0x740=0xd3, PMU PWR=0x7d, NOC_AUTO=0xfffb, BUS_IDLE=0x1ea on BOTH; boot-dmesg 0xff/0x1fa were de-idle transients). Probe orders equivalent; ftrace not built (used drivers' logs+/dev/mem). ⇒ **engage gate below ALL observable register state** (power/clk/PVTPLL/NoC/IOMMU/config/regcmd/submit all matched) = exhaustive HW-internal re-confirm. [[engage-broadcast-openable-2026-06-18]] 2026-06-19c. **LIVE reply DRAFTED `upstream-prep/gahingwoo-probeorder-reply.md` (NOT posted). ▶ NEW ask + next experiment: dynamic ARM-then-PULSE timing/recency** — re-arm CNA/CORE 0x1004/0x3004=0xe as LAST write before OP_EN pulse (fresh-at-head like RK3576), check bit16. Only dimension left besides genuine silicon. = last unexplored dimension → if yes, full ordered probe-time trace (BSP l1 vs rocket) is the next experiment. (= our last un-ruled-out class; WTRACE (B) non-NPU half never captured). TODO cheap: dump whole output BO for high-ch suffix. See [[gahingwoo-cbuf-iommu-wall-2026-06-17]], [[firstconv-weight-packing-bug-2026-06-17]].

## Board Connection
- **Board**: Radxa ROCK 3B with RK3568 SoC, **8GB RAM**
- USB Ethernet IP (office): 10.160.1.44 (may change on reboot; confirmed May 7)
- Home LAN IP: 192.168.1.163 (may change on reboot)
- **WIRED eth0 = 192.168.1.109 (STABLE — use this; cached in ~/.rock3b_ip 2026-06-22).** WiFi wlan = 192.168.1.163 (FLAKY, drops ~1-2 min — avoid). Tailscale = <BOARD-TAILSCALE-IP> (LIVE). ("<TAILSCALE-IP>" May 7 = stale.)
- User: radxa, Password: radxa
- Use `sshpass -p radxa ssh-copy-id` after host key changes (reboot/reflash)
- SSH key gets invalidated when U-Boot is reflashed
- NOPASSWD sudo configured

## NPU Bus De-idle: Full Solution

### Why It Works
- PVTPLL clock (started by SCMI `clk_npu_set_rate()`) must be running BEFORE de-idle request
- Without PVTPLL, NPU hardware has no clock and cannot respond to PMU de-idle ACK
- Once PVTPLL is running (via initial SCMI set at `of_clk_set_defaults` time), PMU de-idle works
- SCMI `clk_npu_set_rate()` does NOT do PMU de-idle itself — it only configures PVTPLL/CRU

### Two-step clk_set_rate in rknpu_power_on
- clk_set_rate(600MHz) then clk_set_rate(1GHz) in rknpu_power_on after clk_bulk_prepare_enable
- Forces SCMI call to TF-A even if kernel clock framework would skip "unchanged rate"
- Required because SCMI disable/enable are no-ops in TF-A; rate must change for SCMI call to fire

### PMU de-idle in rknpu_power_on (KEY FIX)
- `rknpu_noc_deidle()` called unconditionally in rknpu_power_on and rknpu_soft_reset
- Called AFTER clk_bulk_prepare_enable + SCMI two-step rate change (PVTPLL running at that point)
- Sequence: disable NOC auto-idle (NOC_AUTO_CON0 bit2) → request de-idle (BUS_IDLE_SFTCON0 bit2=0) → wait for ACK
- Result at probe: `BUS_IDLE_ST=0x00000072` (bit2=0 = bus ACTIVE)

## TF-A SCMI Fix Applied
- Fixed `clk_scmi_npu_get_rate()` to return `sys_clk_info.npu_rate` when PVTPLL active (bit 15)
- Original code returned 0 when PVTPLL mode enabled, breaking kernel clock framework
- File: `arm-trusted-firmware/plat/rockchip/rk3568/drivers/scmi/rk3568_clk.c`
- Rebuilt TF-A and flashed to eMMC (backup at `~/uboot-mainline/uboot-backup.bin`)

## TF-A EL3 FIQ Catch-all Handler Fix (Boot Loop Recovery, 2026-03-25)
- **Root cause**: rkbin DDR blob (LPDDR4X v1.23) configures LP4 derate temperature-polling DFS triggers
  that fire as Group 0 (EL3/FIQ) interrupts after BL31 hands off to U-Boot
- **Symptom**: Intermittent boot loop — U-Boot crashed at random checkpoint (E or H), never same spot
- **TF-A panic**: `assert(intr_type_descs[INTR_TYPE_EL3].handler != NULL)` in `bl31/interrupt_mgmt.c:159`
- **Fix**: Added `rockchip_el3_fiq_handler()` (no-op: just calls `plat_ic_end_of_interrupt(id)`)
  registered via `register_interrupt_type_handler(INTR_TYPE_EL3, ...)` in `bl31_platform_setup()`
- **File**: `trusted-firmware-a/plat/rockchip/common/bl31_plat_setup.c` (local build dir)
- Also switched from rkbin BL31 to upstream TF-A v2.14.0 (rkbin BL31 v1.45 corrupts EL2 context)
- U-Boot switched from old BSP (TEXT_BASE=0x800000) to mainline 2025.07-rc4 (TEXT_BASE=0xa00000)
- eMMC flashed: `sudo dd if=u-boot-rockchip.bin of=/dev/mmcblk0 seek=64 conv=fsync`
- **Debug code to clean up**: checkpoint letters A-J, e0-e5 in board_f.c; driver name prints in device.c

## U-Boot Status (post recovery)
- Running: mainline U-Boot 2025.07-rc4-dirty (Mar 25 2026)
- BL31: upstream TF-A v2.14.0 with EL3 FIQ catch-all handler
- TPL (DDR init): still rkbin LPDDR4X blob (required, no mainline replacement)
- Build location: /home/dio/Downloads/uboot-build/
- ALWAYS verify eMMC present (`lsblk | grep mmcblk`) before issuing dd flash command

## PMU Write-Mask Protocol
- Upper 16 bits = write enable mask, lower 16 bits = value
- To SET bit N: write `(1 << (N+16)) | (1 << N)`
- To CLEAR bit N: write `(1 << (N+16)) | 0`
- NOC_AUTO_CON0, BUS_IDLE_SFTCON0, PWR_GATE_SFTCON all use this

## Key PMU Registers for NPU (bit positions)
- NPU power: PWR_DWN_ST bit1 (0=ON, 1=OFF)
- NPU bus idle: BUS_IDLE_ST bit2 (0=ACTIVE, 1=IDLE)
- NPU auto-idle: NOC_AUTO_CON0 bit2
- NPU voltage gate: VOL_GATE_SFTCON bit1

## Build Patterns
- Kernel (full, native on board): `make ARCH=arm64 CROSS_COMPILE= -j4`
- Kernel (cross-compile, LXC server): `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j32 Image`
- Kernel (fast): `make ARCH=arm64 CROSS_COMPILE= localmodconfig` first, then `-j4` — much faster
- Rocket module only (native): `make ARCH=arm64 CROSS_COMPILE= -j4 M=drivers/accel/rocket`
- U-Boot: `make CROSS_COMPILE=aarch64-linux-gnu- BL31=... ROCKCHIP_TPL=... -j4`
- TF-A: `make PLAT=rk3568` (on board, native compile)
- Flash U-Boot: `sudo dd if=u-boot-rockchip.bin of=/dev/mmcblk0 seek=64 conv=fsync`
- Install rocket.ko: `sudo cp rocket.ko /usr/lib/modules/6.19.0-rc5/kernel/drivers/accel/rocket/`
- Install Image: `sudo cp arch/arm64/boot/Image /boot/Image-6.19-rocket`

## Cross-Compile / LXC Server
- LXC Tailscale IP: <TAILSCALE-IP>, user root, 32 cores
- aarch64-linux-gnu-gcc installed; kernel source at ~/rock3b-build/linux-mainline/
- Has .config with DRM_ACCEL_ROCKET=m and ROCKCHIP_IOMMU=y
- Board Tailscale IP <TAILSCALE-IP> reachable from LXC directly
- **Workflow**: rsync from LXC pulling board → delete ARM64 scripts binaries → cross-compile → scp Image to board
  - Rsync: `ssh root@<TAILSCALE-IP> "rsync -az --delete radxa@<TAILSCALE-IP>:~/npu-port/linux-mainline/ ~/rock3b-build/linux-mainline/"`
  - Delete ARM64 binaries: `find scripts/ -type f -executable -exec file {} \; | grep 'ARM aarch64' | cut -d: -f1 | xargs rm -f`
  - Build: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j32 Image`
  - Deploy: `sshpass -p radxa scp arch/arm64/boot/Image radxa@<TAILSCALE-IP>:~/Image-6.19-rocket-new`

## NPU Clock Architecture (RK3568)
- GPLL = 1188MHz (not 1200MHz!)
- NPU uses PVTPLL for rates 600-1000MHz (ring oscillator at GRF+0x740)
- For rates <=400MHz, uses GPLL integer divider only
- PVTPLL table: 600(0xfb), 700(0xdb), 800(0xd3), 900(0xd3), 1000(0xd3)
- OTP calibration adjusts PVTPLL lengths per-chip
- `assigned-clocks` MUST include SCMI for NPU (triggers TF-A clk init + de-idle)
- CRU CLK_NPU needed alongside for bus clock derivation (aclk_npu, hclk_npu)
- Kernel cmdline needs `cma=128M` for NPU memory allocation (default 32M too small)

## NPU Benchmark Reference (YOLOv5s-640x640, RK3568)
- 400 MHz (CRU/GPLL): ~120ms
- 600 MHz (CRU/GPLL): ~82ms
- 800 MHz (PVTPLL/SCMI): ~74ms @ 900mV
- 1000 MHz (PVTPLL/SCMI non-IOMMU): ~69ms @ 1000mV
- 1000 MHz (PVTPLL/SCMI **IOMMU mode**): **~57ms** @ 1000mV ← FASTER with IOMMU!
- MobileNetV1: 7.98ms (IOMMU mode, 1GHz)
- Demo requires running from correct dir (cd to rknn_yolov5_demo) or segfaults
- librga.so: install to /usr/lib/ from 3rdparty/rga/RK356X/lib/Linux/aarch64/

## Current Boot Config
- l1 (IOMMU, default): Image-6.19 + rk3568-rock-3b-6.19-iommu.dtb (BSP rknpu driver, all fixes)
- l2 (non-IOMMU, fallback): Image-6.19 + rk3568-rock-3b-6.19-npu.dtb
- l3 (Rocket driver test): Image-6.19-rocket + rk3568-rock-3b-6.19-rocket-fixed.dtb
  - Image-6.19-rocket: board-native build May 7 13:07, kernel `6.19.0-rc5-00003-gb16c04e5e619-dirty`
  - Module at `/lib/modules/6.19.0-rc5-00003-gb16c04e5e619-dirty/extra/rocket.ko`
- Kernel cmdline (l1/l2/l3): `clk_ignore_unused cma=128M`

## Rocket NPU Status: BUG A ACTIVE (May 7)

- [Crash root cause + fix](project_crash_rootcause.md) — iommu attach crash FIXED (Apr 18/19)
- BUG B FIXED (Apr 15): detach-before-attach + attached_domain tracking
- iommu attach crash FIXED (Apr 18/19): hw_needs_enable guard in rk_iommu_attach_device
- **TRM extract created** (May 7): `docs/npu-trm-registers.md` — **MANDATORY reference for all driver work**

### BUG A Current State (May 7): 3 candidate root causes from TRM analysis

**Observed symptoms** (all 13 MobileNetV1 jobs):
- IOMMU STATUS=0x01 immediately after OP_EN (AXI replay stuck — PA in replay buffer, DRAM read never completes)
- PC_interrupt_raw_status=0x4000 (bit14=mmu_error) fires at ~10ms post-OP_EN (AXI timeout)
- CNA_S_STATUS=0 at timeout (CNA never started)
- BOUNCE fires correctly (PA 0x041d1000 → staging PA 0x00514000) but both fail identically → not address-range specific
- REGCMD DATA visible via phys_to_virt → cache coherency NOT the root cause

**Root cause candidates (priority order)**:
1. **PC register write order violation** (TRM §9.5.5 Fig.9-2): TRM mandates INTERRUPT_CLEAR+MASK first,
   THEN addr/amount/op_en. Current code writes BASE_ADDRESS and AMOUNTS BEFORE interrupt_clear — may
   confuse PC state machine. Fix: reorder writes per TRM sequence.
2. **Missing IRQ handler**: `rocket_core_init` never calls `devm_request_threaded_irq`. PC raises IRQ
   (PC_RS=0x4000) at ~10ms; without handler the IRQ line stays asserted, PC DMA may stall before
   broadcasting OP_EN to sub-units. Fix: register IRQ handler.
3. **TASK_DMA_BASE_ADDR wrong**: Current code writes `task->regcmd` (IOVA=0xd7000) to TASK_DMA_BASE_ADDR.
   TRM says final_AXI_addr = base_addr + regcmd_offset. If Mesa writes absolute IOVAs in regcmd data
   fields, all data DMAs go to 0xd7000+iova (wrong). Fix: set dma_base_addr=0 if Mesa uses absolute IOVAs.

**Known bugs still needing fixes**:
- BOUNCE smp_wmb() → must replace with `dma_sync_single_for_device(core->dev, staging_pa, PAGE_SIZE, DMA_TO_DEVICE)`
- PC register write ordering (see #1 above)

### Current boot state (May 7)
- Image-6.19-rocket: board-native build May 7 13:07, kernel `6.19.0-rc5-00003-gb16c04e5e619-dirty`
- Module at `/lib/modules/6.19.0-rc5-00003-gb16c04e5e619-dirty/extra/rocket.ko` (extra dir confirmed)
- Contains: BOUNCE code, PRE_OP pagewalk, PRE_OP REGCMD_DATA dump, full diagnostic readbacks
- extlinux: default=l3

### Key code locations
- `rocket_job.c:168`: TASK_DMA_BASE_ADDR write (potential bug)
- `rocket_job.c:~180-320`: BOUNCE + PRE_OP blocks + full diagnostic readbacks
- `rocket_drv.c`: scale=1, iommu_dev, rk_iommu_trigger_enable/disable
- `rockchip-iommu.c`: hw_needs_enable guard, CMD_DISABLE_STALL

## Upstream NPU Strategy
- BSP RKNPU driver CANNOT be submitted upstream (proprietary userspace)
- Correct path: Add RK3568 support to Rocket driver (accel/rocket, merged 6.18 for RK3588)
- RK3568 NPU = same NVDLA-derived IP as RK3588 (1 core 0.8 TOPS vs 3 cores 6 TOPS)

## LXC Compile Server
- Tailscale IP: <TAILSCALE-IP>, user root
- 32 cores, aarch64-linux-gnu-gcc; kernel source at ~/rock3b-build/linux-mainline/
- SSH key auth between LXC and board (no password needed)
- After rsync from board: delete ARM64 host binaries before building
- LXC builds produce kernel version `6.19.0-rc5` (no git hash, no .git dir)
- Board modules installed at /usr/lib/modules/6.19.0-rc5/ (safe tar: --strip-components=3 -C /usr/lib/modules/)

## Patch Submission Notes
- SOB order: Co-developed-by + their SOB first, submitter's SOB LAST
- AI assistance: use `Assisted-by: Claude:claude-3-opus` (NOT Signed-off-by)
- Submitter full name: Midgy BALON <midgy971@gmail.com>
- PI6C20100 is a clock generator, not a regulator (NAK'd phy-supply patch)
- PI6C20100 v6 sent 2026-03-27: removed regulator-always-on/boot-on, dropped redundant comments, dropped R-b; awaiting Heiko
- **iommu/rockchip GFP_DMA32 patch WITHDRAWN (2026-04-03)**
  - Simon Xue (Rockchip) confirmed: RK356x has TWO IOMMU versions — v1.0 (NPU/ISP) and v2.0 (VOP2 etc.)
  - **Fix path**: land Simon's per-device-ops patch first [xxm@rock-chips.com, 20260310105303.128859]
    then add "rockchip,rk3568-iommu-v1" compatible with .gfp_flags=GFP_DMA32, .dma_bit_mask=DMA_BIT_MASK(40)
  - Offered Tested-by on Simon's patch; reply sent 2026-04-03
- v2 lore: https://lore.kernel.org/linux-rockchip/20260304132957.684616-1-midgy971@gmail.com/

## TRM Reference (MANDATORY)
- **`docs/npu-trm-registers.md`** — RK3568 NPU register extract from TRM Part2 Ch.9
- **Use this file for ALL driver debugging and register programming decisions**
- Contains: PC register map with bit definitions, regcmd 64-bit entry format, GLOBAL_OP_EN,
  interrupt bit assignments (bit14=mmu_error in RAWST), operation flowcharts (Figs 9-2 to 9-6),
  driver bug notes (PC write order, BOUNCE cache, TASK_DMA_BASE_ADDR, missing IRQ handler)

## Rocket Patch Series (patches/rocket-rk3568/)
- 5 patches + cover letter, ready to send after inference confirmed
- Blocking: inference test on l3 boot entry
- Cover letter needs note: requires IOMMU v1.0 fix (Simon's patch) for 8GB+ boards
