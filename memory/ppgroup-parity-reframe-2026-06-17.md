---
name: ppgroup-parity-reframe-2026-06-17
description: gahingwoo reframe — RK3568 executer NOT engaging is CORRECT gating on an empty/misaligned PP group, not a bug; fix = land conv geometry in the consumer's ping-pong group
metadata:
  type: project
---

## ✅ MEASURED 2026-06-17 (board l3) — parity hypothesis NEGATIVE for RK3568
Spliced a per-PP-bank readout into the `group_probe` block (read DATA_SIZE0 0x1020 under POINTER=0
then =1, log consumer ptr + bit16). Built/insmod'd, ran teflon (5×, out_sum 26214400 unchanged):
```
PPPARITY: sp_live=0x0000000e ds0_g0=0x0280002c ds0_g1=0x0280002c sstat=0 exec16=0 cons_ptr=0
```
Decode: S_POINTER=0xe (POINTER/group=0, PP_EN+EXECUTER_PP_EN+PP_MODE set, **bit16 EXECUTER=0**);
**ds0 is POPULATED and IDENTICAL in BOTH PP banks** (0x0280002c — the regcmd-patched geometry, src
~line 883 forces 0x1020→0x0280002c); consumer pointer = group 0 = the group that HOLDS the geometry.
⇒ **Geometry IS in the consumer's group, yet the executer still does not engage.** By gahingwoo's own
`ds0_first` metric we're ALREADY in his "fixed" state (geometry present in the read context), so
**PP-group parity / consumer-group-empty is NOT the RK3568 gate.** This is the "nonzero/nonzero" row
of the diagnostic table → gate is elsewhere (deeper init/arming, or the 2nd weight-staging/CBUF wall).
Caveat: can't fully distinguish "both banks written" from "0x1020 read not bank-switched by POINTER
bit0 from CPU" — but in EITHER interpretation the consumer group is populated, so the conclusion holds.
Consistent with our long-standing finding that rocket is stuck STRICTLY UPSTREAM of RK3576 (RK3576
arms RDMA's executer bit16; rocket arms NONE). **✅ INDEPENDENTLY CONFIRMED 2026-06-17: gahingwoo
WALKED BACK his own parity theory** — warm-state sampling on RK3576 shows geometry latches in the
executer's group (both ping-pong halves) and all four units engage; his `ds0_first=-1` was a cold-kick
artifact (exactly the caveat we flagged). So parity-is-not-the-gate holds on BOTH SoCs. See
[[gahingwoo-cbuf-iommu-wall-2026-06-17]] for his refined CBUF-state localization. Board restored to
baseline (group_probe=0). Probe code
+ `.ko` left installed (instrumentation gated off by default); `patch_ppparity.py` =the splicer;
`upstream-prep/gahingwoo-ppparity-reply.md` updated with this data.

---

**Paradigm shift in the 2-year "compute gate" (2026-06-17, gahingwoo RK3576 feedback).**
For ~2 years we treated "CNA EXECUTER (S_POINTER bit16, 0x1004) never sets on RK3568" as THE bug.
gahingwoo's RK3576 bring-up shows it is almost certainly **correct gating behaviour**, not a fault:

- **RK3576 engages speculatively** — bit16 fires even on an *empty* consumer PP group, then
  fake-dones (finishes too fast, flat output).
- **RK3568 gates on the consumer PP group being *populated*** — it refuses to engage (spins) until
  the conv geometry has actually landed in the group the executer/consumer reads from. So
  "never engages on an empty group" is the EXPECTED RK3568 behaviour.

**The real defect = ping-pong (PP) group parity.** NVDLA CNA has two PP groups (producer side writes
geometry; consumer/executer side reads it). On RK3576 the executer pointer auto-advanced to group 1
while the regcmd wrote geometry (DATA_SIZE0=0x1020, "ds0") into group 0 → split → spin. Fix on
RK3576 = re-run the **state_init CLEAR per-job** (not per-resume), head of every submit, CNA block:
`0x10=1; 0x1004=0(grp0); 0x1024=0x80000000; 0x1004=1(grp1); 0x1024=0x80000000; 0x1004=0x1e`.
The payload is the final **0x1e** — **bit4 POINTER_PP_CLEAR (0x10)** resets producer AND consumer
pointers to 0 so they stop drifting. Result: `cnastart SPTR=0x1000e (ptr=0); cnalive ds0_first=0
ds0=0x30 (h=48); stat_or 0x0c→0x08`.

**Why our prior pp_state_init test (2026-06-15, NEGATIVE) did NOT disprove this:** we measured the
WRONG thing — only bit16 + out_sum. We never measured **which PP group the conv geometry (ds0,
0x1020) landed in vs which group the consumer points at**. gahingwoo's actual diagnostic is reading
DATA_SIZE0 *in the executer's group context* (`ds0_first`/`ds0`). That measurement has NEVER been
taken on RK3568. ⇒ **next experiment is a MEASUREMENT, not a fix.**

**Two RK3568-specific complications (the RK3576 fix will NOT transfer verbatim):**
1. **bit4 POINTER_PP_CLEAR does not exist on RK3568** — `0x1e` reads back `0xe` (confirmed long ago).
   So gahingwoo's exact per-job CLEAR cannot reset our pointers; need the RK3568 equivalent of
   "reset both PP pointers".
2. **RK3568 CNA writes only latch POST-RESET** (opposite to RK3576, where the per-job CLEAR latches
   at submit time). So the injection POINT differs: on RK3568 the PP alignment must be established in
   the **post-reset window**, not at submit head. gahingwoo's port suggestion: do the full state_init
   post-reset so producer+consumer start aligned at group 0; since RK3568 runs a SINGLE task per
   resume (no ping-pong across jobs), they never drift within one run → gate satisfied.

**NEXT EXPERIMENT (board, when up):** after submit, read DATA_SIZE0 (0x1020) under EACH PP group
(select grp via S_POINTER bit0=0 then =1, read 0x1020 each), and read the consumer/executer pointer.
- If geometry is in grp0 but consumer points grp1 (or vice versa) → confirms the parity model →
  fix = stage geometry into the consumer's group post-reset.
- If geometry is absent from BOTH groups → the regcmd geometry writes are being dropped (the
  "latch only post-reset" problem) → fix = replay geometry post-reset.
Then re-check S_POINTER bit16 / out_sum. See [[wtrace-trace-setup]] (BSP reg-write diff still the
complementary line) and [[gahingwoo-state-init-lead-2026-06-10]].

**Forward heads-up — the SECOND gate behind engage (CBUF execution STATE).** ⚠️ CORRECTED 2026-06-17
by gahingwoo (vendor-dump on RK3576): the earlier "`core wt_rd=0` → CMAC never reads weights → bias
rail" theory is a **RED HERRING** — the VENDOR also shows `core wt_rd=0` AND `FDA=0` yet computes a
correct feature map. Real weight read is at **`top wt_rd` (=96)**, not the core counter; FDA=0 is
normal. The true second wall is **CBUF execution state**: gahingwoo made the regcmd byte-identical to
the vendor RUNTIME capture (incl. `FC_CON1=0x777`) and output was INVARIANT → not the command stream.
RK3576 engages but conv0 comes back ~zero (`80 7f 80 80 7f..` vs vendor `80 80 80 86 83 80 88 81..`)
— GENUINELY computes ~0, not a readback artifact. Fix = the vendor's FULL soft-reset (all srsts +
CBUF resets together → IOMMU detach/attach → state_init); standalone CBUF resets fail (per-job
`SRST_A/H_RKNN_CBUF` wipes CNA state + CBUF MMU bank → -14; `SRST_H` once/resume drops CORE_OPEN).
The blocker: full soft-reset asserts `SRST_A_RKNN0` (resets shared MMU) and mainline rk_iommu's
re-attach raw-reset then fails `MMU_DTE_ADDR is not functioning` — vendor rk_iommu survives, mainline
doesn't. ⇒ **driver-level rk_iommu/reset fix.** For RK3568: this is our likely 2nd gate AFTER engage
(we're still upstream — don't even arm). See [[gahingwoo-cbuf-iommu-wall-2026-06-17]].

**CBUF reset lines (do NOT misuse):** `SRST_A_RKNN_CBUF` / `SRST_H_RKNN_CBUF` — vendor pulses them
ONLY in a full soft-reset on error/timeout, **never per-job** (per-job pulse wipes the CNA state
machine → exec_ever back to 0; also resets the CBUF's own MMU bank).

**Attach-once independently VALIDATED by gahingwoo.** The CBUF MMU-bank reset is exactly why per-job
IOMMU attach/detach fails: every attach re-runs rk_iommu_enable's raw MMU reset, which errors once
the MMU bank has been touched → -14 cascade. gahingwoo switched RK3576 to our attach-once (lazy
attach, keep it, re-attach only after a power cycle on a clean MMU) and the IOMMU errors vanished.
Confirms our reset-before-IOMMU-detach + attach-once robustness fixes already in RFC v3.
