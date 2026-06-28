# WRITEUP — RK3568 engage gate SOLVED via BSP-primer — for gahingwoo issue #1 + Tomeu — DRAFT, not posted

Primary: gahingwoo issue #1 (answers the shared "every surface matches, won't engage" thread + the
BSP-primer he asked me to run). Also → Tomeu (email + Mesa MR 42134): it confirms his "known-working
state by submit time" and closes Method B.

---

**The RK3568 engage gate is solved — and it's exactly the "one-time power-up state" hypothesis, pinned
down with a clean experiment. The executers engage on rocket *if and only if* the vendor driver has run
a job first (no power cycle in between).**

## The experiment (BSP-primer swap)
Built one kernel with both drivers as modules (rknpu=m + rocket=m), bound to the same npu@fde40000 via
a combined DT (rknpu's 1-region reg + a single-region fallback in rocket); `modprobe.blacklist` so
neither auto-loads. Then, on one boot, no power cycle:

1. `modprobe rknpu` → run conv2d through the vendor → engages + computes (distinct=24). **= prime.**
2. `rmmod rknpu` (NPU power domain is `always_on`, so the hardware stays powered).
3. `modprobe rocket` → replay the vendor's byte-exact cmdstream (the same Method-B replay that stalls
   cold).

**Result: rocket ENGAGES.** All five units `S_POINTER = 0x0001000e` (bit16 set), `exec_ever=0xf`,
`S_STATUS` CNA=0x08 / CMAC/ACCU/DPU/RDMA=0x0c — byte-matching the working rknn engage signature. Config
now lands (`DATA_SIZE0=0x500050`, `CONV_CON2=0x70`, `WEIGHT_SIZE2=0x05050080`), which it *never* did
cold. Every prior cold run (two years): bit16=0 on every unit, config didn't land.

## The clincher — it's the JOB, not the init
- `modprobe rknpu` **probe-only (no job)** → rmmod → rocket → **no engage** (bit16=0, exec_ever=0x0).
- `modprobe rknpu` **+ run one job** → rmmod → rocket → **engage**.

So it is not rknpu's probe/init (power/clock/reset/IOMMU setup) — it is the **execution of a full vendor
job** that leaves the NPU in an engaged-capable state, persisting across the driver swap. The first job
cycles the units through their complete engage→compute→complete state machine once, and that "unlocks"
engage for subsequent jobs (even under rocket).

## Why two years of comparison found nothing
This is a *dynamically-established* state, not a copyable register write or DT/clock value — which is
why Method B (vendor's byte-exact cmdstream replayed through rocket → stalls), the full WTRACE/source
diffs, and the register/IOMMU/clock comparisons all came back "identical, still won't engage." There
was nothing static to copy. It also confirms, from three directions at once:
- **Tomeu:** "the NPU must be in a known-working state by submit time."
- **gahingwoo:** "RK3568-specific power-up state" (and your RK3576 engages cold because mesa emits the
  0xf008 global enable there — RK3568 has no 0xf008, so it relies on this job-established state instead).
- **Method B:** the gate is the kernel-driver/runtime environment, not userspace.

## The next wall (now reachable for the first time)
Primed-rocket *engages* but the job still doesn't *complete*: `PC_TASK_STATUS=0xf000` (never 0x11000),
`dt_wr=0` (DPU never writes), output stays zero-point. So we're now exactly at your post-engage stage —
the units engage but don't deliver compute. That's your CBUF→CMAC / SDP coefficient-buffer territory.

## Open question / the real fix
The upstream fix is to make rocket establish that state itself (engage cold, no vendor prime). Since
it's a side-effect of the first job's *execution*, the next dig is: what does a completing vendor job
leave behind — the executer ping-pong fully initialized, a CBUF/SRAM-partition init, a mode latched on
first completion? If either of you has seen what specifically a *first completing job* sets that a cold
unit lacks, that's the lever. (gahingwoo — does RK3576 engage cold from the very first job, or does its
first job also "warm up" something?)

Reproducible harness (l8 boot: rknpu+rocket co-loadable, byte-exact replay) available — happy to share.

---

## Notes (not part of the post)
- Sources: [[BREAKTHROUGH-engage-bsp-primer-2026-06-24]], `upstream-prep/bsp-primer-setup.md`.
- Audience: gahingwoo issue #1 (primary) + Tomeu (email/MR). Trim/keep as the user prefers.
- The 0xf008 mention ties to gahingwoo's 2026-06-24 update (RK3576 global enable; RK3568 has none).
- Pairs with the earlier (still-unposted) drafts: enablemask-reply, sdp-ack, iommu-followup — this
  breakthrough supersedes the "still stuck at engage" framing in those; the user may post just this.