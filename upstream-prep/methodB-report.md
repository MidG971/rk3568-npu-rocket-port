# REPORT (Method B result) — TOMEU VERSION (full, KEEPS the OP_EN_PATCH confound section)

For Tomeu (email IRC thread + Mesa MR 42134). The gahingwoo/issue-#1 public version (confound section
REMOVED) = `methodB-report-gahingwoo.md` (READY TO POST). This full version retains the confound for
Tomeu (rocket author — relevant context on the debug overlay). Board: RK3568 ROCK 3B, mainline rocket.

---

**Method B (hardcoded-payload replay) is done, and it isolates the RK3568 engage gate to the rocket
kernel driver — not mesa, not the cmdstream.** Summary + the exhaustive ruled-out list below.

## The test
I captured a *working* vendor payload — the exact conv (`conv2d-bsp.rknn`, 5×5 16→128) that the rknn
driver computes correctly (output `distinct=24`, a real feature map) — and replayed its **byte-exact
cmdstream + weights + input through the rocket kernel UABI** (`/dev/accel/accel0`, a ~200-line
submitter: CREATE_BO ×5, patch the 4 address slots to the new BO IOVAs, SUBMIT, compare to golden).

**Result: the identical bytes that compute on rknn stall on rocket** — `PC_TASK_STATUS=0xf000`,
executers never engage (`S_POINTER` bit16 never sets), job times out, output is the zero-point rail.
Tested with `desc_mode` 0 and 1. So userspace (mesa), the cmdstream content, the coefficients and the
input are all eliminated by construction. **The gate is in the rocket kernel driver's submission path.**

## The golden contrast (same cmdstream, opposite outcome)
Both drivers start from a **byte-identical CNA pre-state** (`DUMP_BEFORE_OPEN`: `S_STATUS=0`,
`S_POINTER=0`, `CBUF_CON0=0x44`, `CLK_GATE=0`) and get the same cmdstream:
- **rknn `AFTER_OPEN`: CNA & CORE `S_POINTER=0x0001000e` (bit16 EXECUTER set), `S_STATUS=0x0c` →
  ENGAGED**, computes.
- **rocket: CNA & CORE `S_POINTER=0x0e` (no bit16), `S_STATUS=0` → never engages.**

(Note for anyone instrumenting: `CONV_CON1`/config registers read 0 at AFTER_OPEN on rknn *too* while
it's engaged and computing — they are **not** diagnostic. The only valid engage signal is `S_POINTER`
bit16.)

## Ruled out (each tested clean, on the byte-exact vendor payload via the replay harness)
- cmdstream content (it's the vendor's own working stream)
- descriptor mode (`desc_mode=0`, `TASK_DMA_BASE=0`, vendor-faithful) — still stalls
- CPU S_POINTER pre-arm (`pp_init=0`, broadcast-only arming like rknn) — still stalls
- `TASK_CON` — identical formula both drivers: `((0x6|pp_en) << 12) | task_number`
- `CLK_GATE` (0x1090) — `0x00000000` on **both** rknn and rocket
- extra CPU pre-writes to sub-units — already disabled in rocket; only the PC writes + regcmd remain
- PC submit writes — `PC_DATA_ADDR` (+ the `=1` slave pre-write), `AMOUNTS`, `INT_MASK=0x10300`,
  `INT_CLEAR`, `TASK_CON`, OP_EN pulse — all match rknn `commit_pc`
- power / clock / PVTPLL / NoC steady-state — byte-identical (from earlier probe-order capture)
- IOMMU / BO residency — **see "where it points" below; weak suspect**

## ⚠️ Important confound we found (and fixed)
Our rocket tree had a debug block (`rocket_job.c`, gated only by `attached_domain` = *always on*) that
**unconditionally rewrote the regcmd in-place every job** with stale hardcoded YOLOv5s values
(WEIGHT_SIZE0 0xc800→0x180, CBUF_CON0 0x44→0x17, CONV_CON4→a yolov5s constant, …). It had been
silently mangling our replay/state tests for a long time. We added a `replay_raw` bypass; **all
results above are with it bypassed.** Flagging in case others carry similar debug overlays —
re-validate any "config landed / register state" observations against a clean submit.

## Where it points
The clincher: in the stalled rocket replay, **the units never begin a DRAM read** — the top-level
data-read counter shows only the PC's ~2 KB regcmd fetch (`dt_rd≈0x89×16`), `wt_rd=0`. So the failure
is **upstream of any memory access**: it's the NPU-internal PC↔unit OP_EN handshake (the thing that
sets `S_POINTER` bit16) failing to complete — *before* the units would touch CBUF/DRAM. That makes
IOMMU and BO-residency weak suspects (they gate data access, which happens *after* engage). Everything
the CPU writes and every steady-state register is matched to the working rknn; only the HW-set engage
bit differs.

## The question for you
You both got the executer to engage (rk3588 / rk3576). With **every observable input matched** and the
gate being this pre-memory PC↔unit handshake: **what completes the OP_EN→executer-engage handshake that
wouldn't show up as a distinct register write or a steady-state value?** Tomeu's earlier hint — "trace
*all* MMIO since module load, the NPU must be in a known-working state" — is where we're headed next:
the full module-load→submit **sequence** of clk/reset/PMU/IOMMU-enable ops relative to OP_EN (the NPU
register block itself has *no* init writes on RK3568, so any difference is in the non-NPU init
*ordering*). We have an l7 trace kernel (clk/regmap tracepoints + RKNPU WTRACE) staged to capture the
rknn side and diff the ordering.

## Reusable
The replay harness (`rocket_replay.c`) submits arbitrary `{cmdstream, BOs}` through rocket and A/Bs
kernel tweaks against the rknn golden — happy to share if useful for isolating on your side.

---

## Notes (not part of the post)
- Sources: [[tomeu-engaged-payload-replay-2026-06-22]] (Method B + drilling), [[bsp-engage-headarm-2026-06-19]]
  (engage signature), [[engage-broadcast-openable-2026-06-18]] (arming history).
- Artifacts: rocket_replay.c, regcmd_from_dump.py, rknn_raw_run.c, patch_replayraw.py;
  for_review/conv2d-{regcmd,weights,golden-out}.bin, methodB-payload-manifest.md, methodB-build-status.md.
- Audience: gahingwoo issue #1 (primary, collaboration hub) + Tomeu (email IRC thread + Mesa MR 42134 —
  it answers his Method B directly). Possibly trim the confound section for the public post; keep for Tomeu.
- Next experiment: l7 capture (wtrace-trace-setup.md FINALIZED block) → init/IOMMU-sequence diff.
- Board: l3 baseline, healthy. Hazard noted: don't rmmod rocket after a replay (hangs on the stuck job).