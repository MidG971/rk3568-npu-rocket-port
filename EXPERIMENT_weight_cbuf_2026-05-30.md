# Experiment: weight → CBUF load path (2026-05-30)

## Why (one paragraph)
Per TRM, `CNA_s_status=0x1` = "Executer 0 operating" — CNA **runs**, it does
not stall. Current best state (option_b2: high-IOVA BOs, aligned page tables,
clean IOMMU, configs land) gives uniform `128` output = MAC product is **zero**
for every output element. Weight BO + input BO both contain valid data and
their IOVAs resolve (DPU writes land). So the MAC is getting zero weights (or
zero features) from CBUF. Prime suspect: **CBUF bank allocation** — mesa emits
`CBUF_CON0=0x84` → `weight_bank=0` (no banks reserved for weights). BSP emits
`0x17` → `weight_bank=1`. No weight banks ⇒ weights never buffered ⇒ MAC×0.

Every prior CBUF/weight test happened BEFORE the option_b2 IOMMU fix, when CNA
configs weren't landing at all — so those nulls were meaningless. This is the
first time the test can actually take effect.

## Prereqs
- Board on **l3** (Image-6.19-rocket + rk3568 rocket DTB).
- Live rocket source = option_b2 (alloc_iova_fast, BOs at high IOVA). Confirm
  AT10MS dump shows regcmd≈0xffe7e000, output≈0xffe80000, faults≈0–1/run.

## Step 1 — Baseline + readback (no change yet)
Add AT10MS readbacks (most already present) for:
`CBUF_CON0 (0x1040)`, `CBUF_CON1 (0x1044)`, `WEIGHT_SIZE0 (0x1030)`,
`WEIGHT_SIZE1/2`, `DCOMP_ADDR0 (0x1110)`, `DCOMP_CTRL (0x1100)`.
Run conv2d teflon. Record values + output histogram.
✅ Expect: CBUF_CON0=0x84 (weight_bank=0), DCOMP_ADDR0=weight IOVA, output=128.

## Step 2 — Force weight_bank via CBUF_CON0 (the test)
CNA config regs are PC-broadcast-only (CPU writes dropped), so patch the
**regcmd** in-kernel (kmap_local_page primitive) to override the CBUF_CON0
entry value: try in order, one per run, re-reading output each time:
- `0x17`  (BSP YOLOv5s value: weight_bank=1, data_bank=7)
- `0x1017` (BSP + data_reuse bit 12)
- `0x27`  (weight_bank=2 = banks 6/7) — our conv weights are larger than YOLO's first conv
✅ Win condition: output histogram changes from uniform 128 (MAC starts
producing non-bias products). Then compare to CPU ref sum=23,252,430.

## Step 3 — If CBUF_CON0 alone doesn't move it
The weight-bank field may need pairing with `CBUF_CON1` (entries_per4surf /
data_entries) so feature+weight banks don't overlap. Capture BSP CBUF_CON1 for
a comparable conv; patch rocket to match.

## Step 4 — Confirm against BSP (separate boot)
Boot l1 (BSP), run conv2d.tflite via RKNN if convertible, OR reuse the existing
MobileNetV1 BSP capture, and read CBUF_CON0/CON1/weight_bank for a 3×3 conv.
Goal: know the correct weight_bank value for OUR shape, not just YOLO's.

## Mesa-side fix (once a value works)
In `rkt_regcmd.c` the value comes from `task->weights_banks`. Combined with the
already-drafted `CBUF_BANKS 12→8` change (RK3568 has 8 banks), recompute
`weights_banks` so it is ≥1 for RK3568. That is the real userspace fix; the
kernel regcmd patch only proves the hypothesis.

## Notes / guardrails
- Do NOT CPU-write CNA config regs (0x10xx) — confirmed board-hang risk; patch
  regcmd instead.
- One variable per run; re-read output histogram each time.
- If board hangs, power-cycle (PMIC latches — unplug power+USB 60s).
