# PP-group parity diagnostic (RK3568) — the measurement we've never taken

Origin: gahingwoo RK3576 feedback 2026-06-17. The RK3568 executer refusing to engage (S_POINTER
bit16 never set) is likely CORRECT gating on an empty/misaligned ping-pong (PP) group — NOT the bug.
The defect is parity: conv geometry (DATA_SIZE0, 0x1020) landing in a different PP group than the
consumer/executer reads from. We have only ever measured bit16 + out_sum. We have NEVER measured
*which PP group the geometry is in vs where the consumer points*. This adds exactly that.

## What to add (live rocket_job.c on board, AT10MS dump window — same `_npu = ioremap(0xfde40000,...)`)

```c
/* PP-GROUP PARITY PROBE (2026-06-17, gahingwoo reframe).
 * Read CNA DATA_SIZE0 (0x1020 = ds0 geometry) under BOTH ping-pong group contexts,
 * and the live executer/consumer pointer. Goal: is geometry in the group the
 * executer reads from? S_POINTER POINTER=bit0 selects the register bank to read.
 * NOTE: CPU writes to S_POINTER DO latch on RK3568 (we've seen 0xe readback), so
 * toggling bit0 to select a bank is observable. Save+restore S_POINTER around it. */
{
	u32 sp_save = readl(_npu + 0x1004);
	u32 sp_live = sp_save;
	u32 ds0_g0, ds0_g1, sstat;

	/* select group 0, read its geometry */
	writel((sp_save & ~0x1u) | 0x0, _npu + 0x1004);
	ds0_g0 = readl(_npu + 0x1020);
	/* select group 1, read its geometry */
	writel((sp_save & ~0x1u) | 0x1, _npu + 0x1004);
	ds0_g1 = readl(_npu + 0x1020);
	/* restore */
	writel(sp_save, _npu + 0x1004);

	sstat = readl(_npu + 0x1000);   /* CNA_S_STATUS: STATUS_1[17:16], STATUS_0[1:0] */
	pr_info("PPPARITY: sp_live=0x%08x ds0_g0=0x%08x ds0_g1=0x%08x sstat=0x%08x exec_bit16=%u consumer_ptr=%u\n",
		sp_live, ds0_g0, ds0_g1, sstat,
		!!(sp_live & 0x10000),       /* EXECUTER engaged */
		!!(sp_live & 0x1));          /* POINTER (group select) */
}
```

`ds0` decode (REG_CNA_DATA_SIZE0 = 0x1020): DATAIN_WIDTH = bits[26:16], DATAIN_HEIGHT = bits[10:0]
(see rocket_registers.h). gahingwoo's `ds0=0x30 (h=48)` is the height field non-zero = geometry present.

## How to read the result

| ds0_g0 | ds0_g1 | meaning | next move |
|---|---|---|---|
| non-zero | 0 | geometry in group 0 only | check `consumer_ptr` — if it points group 1 → **parity split confirmed** (gahingwoo's RK3576 case). Stage geometry into the consumer's group post-reset. |
| 0 | non-zero | geometry in group 1 only | same, mirror image |
| non-zero | non-zero | geometry in both | parity is fine here — gate is elsewhere (weight-staging / CBUF, the 2nd wall) |
| 0 | 0 | **geometry in NEITHER** | regcmd's DATA_SIZE writes are being DROPPED (the RK3568 "latch only post-reset" problem) → fix = replay geometry in the post-reset window, not via regcmd at submit |

## Why this matters / why prior pp_state_init test didn't settle it
2026-06-15 we ran `pp_state_init` per-resume and saw "no bit16" → called it negative. But no-bit16 is
the EXPECTED gated state if the consumer's group is empty. The discriminator is ds0-per-group, above.

## RK3568 caveats (the fix won't be gahingwoo's verbatim 0x1e)
- bit4 POINTER_PP_CLEAR (0x10) doesn't exist on RK3568 → `0x1e` reads back `0xe`. Can't reset both
  pointers the RK3576 way.
- RK3568 CNA writes latch only post-reset → PP alignment must be set in the post-reset window, not at
  submit head. Single task per resume ⇒ if aligned post-reset, no drift within the run.

## After this measurement
If parity-split confirmed: in `rocket_core_pp_state_init` (post-reset), ensure both producer and
consumer pointers end at group 0 AND that the subsequent regcmd geometry writes target group 0. If
geometry-in-neither: move the DATA_SIZE0/1 (and likely the full CNA geometry block) emission into the
post-reset window. Re-measure ds0-per-group + bit16 + out_sum each time.

Complementary line still open: BSP l1 RKNPU_WTRACE reg-write diff (see wtrace-trace-setup.md).
