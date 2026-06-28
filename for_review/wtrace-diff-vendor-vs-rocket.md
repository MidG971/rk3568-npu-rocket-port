# WTRACE diff: vendor RKNPU vs rocket on RK3568 (2026-06-25)

Captured vendor (BSP RKNPU) job-commit MMIO via WTRACE on l7 (YOLOv5s, 10 loops, 396 writes),
plus source-level power-on comparison (clk/regmap tracepoints aren't built into the trace kernel,
so the non-NPU CRU/PMU writes come from rknpu_drv.c source — the deterministic result of the
driver's clk/PMU calls).

## A. Job-commit (job-time) writes — vendor does LESS than rocket
Vendor RK3568 commit = 8 PC offsets only (all sub-unit writes come from the regcmd via PC broadcast):
- 0x10 DATA_ADDR (HIGH IOVA 0xffef/0xfff2), 0x14 AMOUNTS, 0x20 INT_MASK=0x10300, 0x24 INT_CLEAR,
  0x30 TASK_CON, 0x34 TASK_DMA=0 (no descriptor), 0x08 OP_EN pulse 1->0, 0x8010 perf clr.
- **NO 0x1004/0x3004 S_POINTER arm** (gated `num_irqs>1` = RK3588 multicore; single-core RK3568 skips).
- **state_init = NULL** for ALL rk356x configs (rk3576_state_init is RK3576-only).
ROCKET does a SUPERSET: arms S_POINTER (pp_init), optional descriptor, etc.

## B. Power-on (probe/resume) — concrete divergences
| # | Vendor rknpu_power_on | Rocket runtime_resume |
|---|---|---|
| 1 | two-step **raw clk_set_rate(clks[0]=SCMI, 600MHz)** -> target | two-step **opp_set_rate(200MHz)** -> target |
| 2 | method = raw clk (clock only) | method = OPP (clock+regulator coupled) |
| 3 | regulator_enable(**vdd** + **mem/sram**) before clk | OPP handles vdd only (no mem/sram enable) |
| 4 | order: reg -> clk_en -> rate -> noc_deidle -> iommu | clk_en -> opp -> noc_init -> iommu detach |
NPU PMU de-idle (0xfdd90000 +0x70/0xa0/0x50, poll 0x68) is equivalent in both.

## C. KEY LOGICAL NARROWING (breakthrough + WTRACE together)
The BSP-primer breakthrough showed **probe-only does NOT prime; a JOB RUN is required**. rknpu's
probe already does the FULL power-on (B). So if power-on were the gate, probe-only would prime — it
doesn't. ⇒ **the power-on differences (B) are NOT the gate.** The engage is established by the JOB.

The job = regcmd (Method B: byte-identical) + driver writes (A). Vendor's job writes are MINIMAL and
rocket already matches/exceeds them. So with identical regcmd + matching-or-superset driver writes,
the only remaining variable is: **rocket does MORE than the vendor on a COLD NPU** (arms S_POINTER,
pp_init, descriptor). Once PRIMED, rocket's extra writes don't block engage (primed+rocket=engage).
But on a COLD NPU the extra arming may mis-sequence the first engage.

## D. NEXT EXPERIMENT (well-motivated, untested)
**Strip rocket's cold submission to the vendor's EXACT minimal sequence** — no S_POINTER arm, no
pp_init, no descriptor (desc_mode=0), exact 8 PC writes in vendor order/values (incl. HIGH IOVA for
the regcmd) — then replay the vendor regcmd on a COLD NPU. This is the true "do exactly what the
vendor does, nothing more" test. If it engages cold => the gate was rocket's extra cold-arming. If
not => the gate is the regcmd BO/IOVA setup or something still HW-internal (back to primer-as-fix).
(Pieces tried separately before — desc_mode=0, HIGH IOVA, with/without pp_init — but never ALL
minimised together + exact vendor PC order on cold.)
