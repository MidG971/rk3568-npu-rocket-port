# Hypothesis C test — GRF/PMU rocket(l3) vs rknpu/BSP(l1) — 2026-06-10

**Verdict: RULED OUT.** The NPU's GRF + PMU configuration is byte-identical between the
working vendor `rknpu` driver (l1, kernel #68, RKNPU 0.9.8 built-in, YOLOv5s 90ms working)
and the non-working mainline `rocket` driver (l3). No clock-gate / power-routing bit is left
unconfigured by rocket.

## Method
- Compiled an mmap `/dev/mem` register dumper (`/tmp/rdump.c`, kept at `~/rdump` on board).
  `STRICT_DEVMEM=y` but `IO_STRICT_DEVMEM` unset → device-MMIO syscon reads allowed.
  Avoided the NoC region `0xfe18xxxx` (known to hang on userspace read).
- Dumped PMU `0xfdd90000`(0x100) + PMUGRF `0xfdc20000`(0x100) + SYS_GRF `0xfdc60000`(0x600)
  = 512 registers. NPU in active state on both (l3: post-teflon; l1: post-YOLOv5s).
- Validated dumper: PMU `0x68 = 0x1ea` matches the kernel's own readback on both.

## Result
- `diff l3 vs l1` → **exactly 1 register differed**: `SYSGRF 0xfdc60420` (l3 `0x003fa104`
  vs l1 `0x003fa108`).
- That register is **volatile**: re-reading it gives `0x...109 / 0x...102 / 0x...104 / 0x...14c`
  (low byte free-running — a PVTM/ring-oscillator-style live counter in SYS_GRF).
- Same-kernel diff (l1a vs l1b) shows the only volatile regs are `0xfdc60420` + `PMU 0xfdd9008c`.
  So the single l3-vs-l1 delta is pure sampling noise.
- PMU power/NoC values identical on both: `0x50=0x62, 0x68=0x1ea, 0x98=0x7d, 0xa0=0x7d`.

## Conclusion
Zero stable GRF/PMU config differences. Closes the power/clock/gating layer entirely
(adds to: clk_summary match, PMU NoC de-idle match). The compute gate is NOT a GRF/PMU
clock-gating or power-routing difference. Files: `grfpmu_l3.txt`, `grfpmu_l1_rknpu.txt`.
