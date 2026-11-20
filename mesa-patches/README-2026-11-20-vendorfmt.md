# mesa RK3568: vendor-format A/B/C coefficient buffer + BS regcmd (2026-11-20)

`mesa-coefs.patch` (rkt_coefs.c) + `mesa-regcmd.patch` (rkt_regcmd.c), against the
mesa 25.3 tree used on the board.

- Coefficient buffer (default on; `ROCKET_NO_ABC=1` escapes): per 8 output channels one
  64 B record `[8xi32 A | 8xi16 B | 8xi16 C]`; A = bias - (in_zp-0x80)*weight_sum;
  B = shift (0 for per-tensor weights); C = Q14 multiplier relative to the max channel
  scale (max 16384); absolute scale stays in DPU OUT_CVT. Format decoded from a vendor
  capture's bytes + register field widths.
- BS regcmd per-tensor defaults `BS_CFG=0x158`, `BS_ALU_CFG=0`, `BS_MUL_CFG=0` —
  byte-identical to the working vendor regcmd; `ROCKET_PER_CHANNEL=1` un-bypasses BS_MUL
  (`BS_MUL_SRC|BS_TRUNCATE_SRC`) for per-axis models. `BRDMA_DATA_USE=7` with the buffer.

Offline-validated on a ROCK 3B (dumped BO + regcmd vs vendor captures): buffer record
shape matches, `0x4040=0x158`, OUT_CVT scale/shift give the exact requant ratio.
End-to-end output still waits on the kernel-side compute gate (see README Status).
Open diffs vs the vendor capture, flagged for review: OUT_CVT_OFFSET, 0x5044 FEAT_MODE.
