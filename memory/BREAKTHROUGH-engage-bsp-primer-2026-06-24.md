---
name: BREAKTHROUGH-engage-bsp-primer-2026-06-24
description: 🎉 THE 2-YEAR ENGAGE GATE CRACKED. BSP-primer (rknpu prime → rmmod → rocket, no power cycle) makes rocket's executers ENGAGE (S_POINTER bit16=0x1000e on all 5 units) for the first time on RK3568. Gate = one-time vendor-established power-up/init state.
metadata:
  type: project
---

# 🎉 ENGAGE GATE SOLVED 2026-06-24 — BSP-primer proves it's vendor-established power-up state

**THE result after ~2 years.** The BSP-primer swap (built l8: rknpu=m + rocket=m co-loadable, single
device) ran on RK3568:
1. `modprobe rknpu` → ran conv2d-bsp (vendor engages+computes, distinct=24) = **PRIME the NPU**.
2. `rmmod rknpu` (NO power cycle — NPU PD is always_on, stays powered).
3. `modprobe rocket` (binds rk3568-rknpu via primer patch, single-region devm_ioremap map).
4. `rocket_replay` (vendor byte-exact cmdstream) →
   **AFTER_OPEN_ENGAGE: CNA=CMAC=CORE=DPU=RDMA = 0x0001000e (bit16 SET = ENGAGED), exec_ever=0xf.**
   ALL_SP: CNA S_STATUS=0x08, CMAC/ACCU/DPU/RDMA S_STATUS=0x0c, all S_POINTER=0x1000e — **byte-matches
   the working rknn engage signature.** Config LANDED (DATA_SIZE0=0x500050, CONV_CON2=0x70,
   WEIGHT_SIZE2=0x05050080) — which NEVER happened cold.

**vs EVERY prior cold rocket run (l3, 2 years): bit16=0 on every unit, exec_ever=0x0, config didn't
land.** The ONLY difference = the vendor prime. ⇒ **the engage gate is ONE-TIME POWER-UP/INIT STATE the
vendor driver (rknpu) establishes and rocket does NOT.** Confirms gahingwoo's "RK3568-specific power-up
state" framing + Tomeu's "NPU must be in a known-working state by submit time." NOT a register write,
not the cmdstream (Method B proved that), not IOMMU/clock — it's HW-internal state set by the vendor's
init/first-job, persisting across the driver swap (no power cycle).

## ⚠️ Engage SOLVED, but COMPLETION still fails (the NEXT wall, now exposed)
Primed-rocket job still TIMES OUT: TASKST=0xf000 (not 0x11000 DONE), PERF dt_wr=0 (DPU never writes),
dt_rd=0x89 (~regcmd only), wt_rd=0, output distinct=1 (BO never written). So units ENGAGE (bit16) but
don't read data / compute / write output. This is the post-engage stage = gahingwoo's territory
(CBUF→CMAC / SDP coefficient buffer [[gahingwoo-sdp-coeff-buffer-2026-06-23]]). Could also be that the
replay's data BOs aren't a complete real compute. But ENGAGE — the 2-yr blocker — is DONE.

## ✅ REFINED 2026-06-24: PRIME REQUIRES A JOB RUN (probe-only does NOT prime)
TEST A (l8, fresh boot): `modprobe rknpu` (probe ONLY, NO job) → rmmod → modprobe rocket → replay =
**NO engage (bit16=0, exec_ever=0x0).** vs rknpu+JOB = engage. ⇒ **the prime is a side-effect of the
vendor EXECUTING A FULL JOB, not rknpu's probe/init.** The first vendor job cycles the units through
their complete engage→compute→complete state machine, leaving the NPU in an engaged-capable state that
persists (no power cycle) for subsequent rocket jobs. NOT a discrete init write (why 2yr of register
comparison found nothing — the state is set DYNAMICALLY by a working job). Three-point ladder:
cold rocket = no engage; rknpu-probe-only + rocket = no engage; rknpu-JOB + rocket = ENGAGE.

## ▶ NEXT (decisive for a REAL fix, not just the primer):
1. ✅ DONE: probe-only does NOT prime; JOB required (above).
1b. **What in the vendor JOB's EXECUTION leaves the persistent state?** Candidates: units completing
   their full state-machine cycle once (PC→0x11000, executer ping-pong fully initialized); a CBUF/SRAM
   partition init done during the job; a global enable/mode latched by the first completing job. Deep
   dig — possibly needs comparing NPU internal state before/after the first vendor job.
2. **COLD baseline on l8** (fresh boot, modprobe rocket WITHOUT rknpu prime) → confirm no engage
   (rigor; we have overwhelming prior l3 evidence but confirm on this kernel).
3. **Find the specific vendor init** rocket misses → port to rocket so it engages WITHOUT the prime
   (the real upstream fix). Candidates: rknpu probe sequence (power/clock/reset/iommu ORDER), or a
   first-job side effect. NPU-register-write audit said no init NPU writes on rk3568 → so it's likely a
   power/clock/reset SEQUENCE or an off-NPU (GRF/PMU) poke at probe, OR a job-execution side effect.
4. Then the post-engage completion wall (gahingwoo's coefficient buffer).

## Writeup + plan (2026-06-24)
- **Writeup for gahingwoo+Tomeu DRAFTED** (NOT posted): `upstream-prep/breakthrough-engage-report.md`
  (supersedes the "still stuck at engage" framing in the unposted enablemask-reply/sdp-ack/iommu-followup
  — user may post just this).
- **Next-move plan DRAFTED**: `upstream-prep/next-move-plan-engage-fix.md`. ▶ FIRST step next session =
  **register-state diff primed-vs-unprimed NPU** (find the persistent delta the vendor job leaves; if
  register-visible → port to rocket → engage cold; if empty → HW-internal, port vendor in-job
  SRAM/CBUF-partition init). Ask gahingwoo if RK3576 engages on the VERY first job or warms up.

## Setup (reusable): l8 boot entry (Image-6.19-primer, rknpu+rocket =m, modprobe.blacklist), primer DTB
(rk3568-rock-3b-primer.dts, 1-region + both compatibles), rocket primer patch (match += rk3568-rknpu +
single-region devm_ioremap fallback). See `upstream-prep/bsp-primer-setup.md`. Board on l8; restore
default l3 after. Serial logging active.
