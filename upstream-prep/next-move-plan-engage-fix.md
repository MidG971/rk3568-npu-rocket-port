# Next-move plan — turn the BSP-primer breakthrough into a real rocket-only engage fix

GOAL: rocket engages COLD (no vendor prime). We know the engage gate = state left by the vendor's
first JOB EXECUTION (probe-only doesn't prime; a job run does). Find what that state is and replicate
it in rocket. All work on the l8 primer kernel (rknpu+rocket co-loadable) — the reproducible harness.

## Experiment ladder (in priority order)

### ⚠️ HAZARD LESSON 2026-06-24: /dev/mem WIDE NPU dump HANGS the board (even under rocket)
Tried `npu_regdump.py` (/dev/mem read of 0x0..0xf00c incl. global 0xf000/0xf008 + perf 0x8xxx) right
after `modprobe rocket` (cold, NPU powered) → **hard NoC hang, no panic, no serial output → power
cycle.** So /dev/mem of the NPU register range — ESPECIALLY the global (0xf000+) and perf (0x8xxx)
regions — is NOT safe even with rocket as the driver (narrower 0x1000-range reads worked on l3, but the
wide dump incl. 0xf008/0x8xxx wedges the bus). **⇒ DO THE REGISTER DIFF VIA A KERNEL-SIDE DUMP, NOT
/dev/mem:** add a comprehensive NPU readback to rocket's PROBE (rocket already ioremaps 0xfde40000 +
reads many regs safely in AT10MS/ALL_SP) — rebuild rocket, capture cold-vs-primed via dmesg. Avoid
/dev/mem for any NPU offset >= 0x8000.

### 1. ⭐ Register-state diff: primed vs unprimed NPU (HIGHEST VALUE, concrete) — USE KERNEL DUMP
Dump the NPU register space BEFORE the first vendor job vs AFTER (primed), find the persistent delta.
- On l8: `modprobe rknpu`; dump NPU regs (kernel readback or, since rknpu is a MODULE now, a small
  debugfs/ioctl OR /dev/mem on l8 — but /dev/mem hung on the BSP kernel; on the primer kernel test
  carefully, or add a kernel dump). Capture state PRE-job.
- Run one vendor job (prime). Dump again POST-job (primed).
- `rmmod rknpu; modprobe rocket`; dump again (rocket sees the primed state).
- DIFF pre-job vs primed. Candidate regions: NPU global (0xf000-0xf010 ENABLE_MASK), CBUF control
  (0x1040 CBUF_CON0, CBUF_CON1, 0x1090 CLK_GATE), the per-unit S_STATUS/S_POINTER, GLOBAL_OP_EN (0x08),
  PVTPLL/PMU. If the primed state shows a register set that cold rocket lacks → port it.
- ⚠️ NPU regs reset between jobs in our experience, so the persistent state may be HW-internal (not
  register-visible). If the diff is empty → it's internal pipeline/SRAM state (go to #2/#4).

### 2. Minimal-prime characterization (understand the mechanism)
- Does the prime job need to COMPLETE, or just engage? (If we can capture a vendor job that engages but
  is killed mid-flight, does it still prime?)
- Does ANY engaging vendor job prime, or only conv2d? (Try yolov5s prime → rocket conv2d replay.)
- Does priming need real data/weights, or any job shape? (Narrows what the "warm-up" touches.)

### 3. Persistence / decay (scoping)
- After prime, run MULTIPLE rocket jobs — does engage persist for all, or decay after one?
- Does forcing NPU autosuspend (idle) lose the prime? (Pin power on = it survives → confirms it's
  power-domain-state, not register.) Our PD is always_on, so it likely survives idle.

### 4. Replicate the job side-effect in rocket (the actual fix)
- If #1 finds register state → write it in rocket's probe/resume → test cold engage.
- If HW-internal: the vendor's first job runs the units through engage→compute→complete once. Hypotheses
  for what "warms up" the executer: (a) the ping-pong executer pointers get fully initialized only by a
  completing job; (b) a CBUF/SRAM partition init the vendor does in-job (gahingwoo's "SRAM partition
  setup before runs"); (c) a mode latched on first PC completion (0x11000). 
  - Test (a): rocket's pp_state_init/force_grp0 we tried statically didn't engage — retry them on l8
    AFTER a vendor probe (clk/iommu up) to isolate.
  - Test (b): port the vendor's SRAM-partition/CBUF-init sequence (get it from rknpu source — grep for
    SRAM/CBUF partition writes in rknpu probe/first-submit) into rocket.
  - Test (c): can rocket get ONE job to complete via a degenerate/tiny job that doesn't hit the gate?
    (speculative; the gate blocks the first job — chicken-egg).

### 5. Ask the experts (parallel)
- gahingwoo: does RK3576 engage on the very FIRST job from cold, or does its first job warm something
  up? What does the vendor's first-job execution leave that a cold unit lacks? (in breakthrough-engage-report)
- Tomeu: on rk3588 (where he got the first conv working), did he need a vendor prime, or did rocket
  engage cold? If cold — what's in his rk3588 init that rk3568 rocket lacks?

## Recommended FIRST step next session
**#1 (register-state diff primed-vs-unprimed)** — it's the only path that could yield a direct
copyable fix, and it's concrete on the l8 harness. If it comes back empty (HW-internal), pivot to #4b
(port the vendor's in-job SRAM/CBUF-partition init) + #5 (expert input).

## Harness state
l8 boot entry ready (Image-6.19-primer, rknpu+rocket=m, modprobe.blacklist). Replay binaries + payload
re-scp each boot (/tmp clears). Board on l8; default reset to l3. Don't rmmod rocket after a replay
(hangs). Serial available.
