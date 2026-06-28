# dump.py capture + vendor-bias.bin decode attempt (2026-06-26)

GOAL: the per-channel coefficient-buffer byte layout (for mesa STEP 2 requant fix).

## dump.py-by-flink capture = FAILS on our rknpu
- Staged allbilly dump.py + registers.xml + gdb to board ~/dumptool (dump.py device patched card1->card2).
- Vendor conv WORKS on rknpu (card2, minor 2): rknn_raw_run conv2d-bsp.rknn → OUT sum=26113009
  distinct=24; dmesg RKNPU_PIPE dt_wr=0x6400 dt_rd=0x32c9 wt_rd=0x1900, all 5 units S_STATUS=0x0c.
- rknn_raw_run RKNN_HOLD=1 holds 90s (GEMs alive). But dump.py GEM_OPEN by flink (1-14) FAILS silently
  (no handle, no dump files) → **rknpu does NOT flink its GEMs**, so GEM_OPEN-by-name can't reach them.
  dump.py-by-flink is a dead end here; needs the gdb /proc/<pid>/maps route OR /dev/mem at the kernel-
  printed PA (RKNN_HOLD comment: "PAs printed by REGCMD_FULL_BSP, set RKNN_HOLD=1") OR a kernel-side BO
  dump (like rocket's WEIGHT_BO) added to rknpu.

## vendor-bias.bin decode = NOT a plain bias buffer
- conv2d-cal: input [1,80,80,16] zp=128, weight [128,5,5,16] scale=3.912546 (tflite_runtime says
  per-TENSOR, nscales=1; bias scale 0.030567=in*wt confirms), output [1,40,40,128] scale=32 zp=128.
- corr = sum(w-wzp)*(input_zp-0x80) = *(128-128) = 0 → stored bias should = raw bias.
- Raw biases [-54,3221,10107,-1021,...] NOT found in vendor-bias.bin (20800B=5200 int32) as int32,
  not consecutive, not at any stride. ⇒ vendor-bias.bin is NOT a simple int32 bias buffer; it's the
  value-dependent coefficient/weight surface gahingwoo mapped (mixed int32 + float32 per the earlier
  dump; ~162.5 B/channel for 128 ch). Encoding non-obvious → needs gahingwoo's record spec.

## ⇒ BLOCKER = the coefficient-buffer record format (gahingwoo's reverse-engineering).
Posted ask: upstream-prep/gahingwoo-code-bug-found-2026-06-26.md (3 layout Qs). Cleanest unblock = his
reply. Heavier self-serve = gdb/proc-mem or rknpu kernel-BO-dump capture (future focused session).
