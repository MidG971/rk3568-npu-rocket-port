# NEXT SESSION PLAN — RK3568 rocket: implement STEP 2 per-channel requant (written 2026-06-27)

## State in one line
**The blocker is GONE.** The coefficient-buffer record format is DECODED (offline from vendor-bias.bin)
AND confirmed against registers.xml field widths. Next session = IMPLEMENT STEP 2 (build the buffer in
the vendor format + flip the BS regcmd) and test by OUTPUT maxdiff. No more reverse-engineering needed.
Refs: [[coeff-buffer-format-decoded-2026-06-27]], [[mesa-per-channel-requant-bug-2026-06-26]],
[[allbilly-repo-bs-rdma-mechanism-2026-06-26]].

## THE FORMAT (confirmed two ways: buffer bytes + register field widths)
Coefficient surface = **8 bytes/channel**. Layout = interleave-by-8: each 64-byte record holds 8 channels
as `[8×int32 bias | 8×int16 shift | 8×int16 mult]`.
- bias  = int32, ALU operand (BS_ALU_SRC=1). For conv2d-cal corr=0 → raw tflite bias.
- shift = stored in int16, but HW field BS_MUL_SHIFT_VALUE is **6-bit [13:8]** (colA, small signed).
- mult  = int16, BS_MUL_OPERAND **16-bit [31:16]** (colB, ~2^14).
conv2d-cal is PER-TENSOR (ws=3.9125, is=0.0078125, os=32) → ratio = ws·is/os ≈ **9.55e-4**, constant.
STEP 1 already computed the fixed-point: **mult=32052, shift=25** (32052·2^-25 = 9.552e-4). All 128 ch same.
(Vendor's colB varies per channel only because RKNN re-quantizes weights per-channel internally — we
keep per-tensor weights, so constant mult/shift is correct for us.)

## 0. Session start (always)
- Board: l8, primed rocket (primer auto-runs at boot). Tailscale <BOARD-TAILSCALE-IP> / `cat ~/.rock3b_ip`.
  RULES: judge by OUTPUT maxdiff only (perf counters are red herrings); **≤2 NPU jobs/burst** (wedge);
  recover wedge via serial SysRq `s`/`u`/`b` → reboots l8 → auto-reprimes. Mesa build ~3min on board
  (background it: `ninja -C ~/mesa/build src/gallium/targets/teflon/libteflon.so`); LXC <COMPILE-SERVER-IP>
  for faster builds. /tmp clears on reboot → re-scp test_conv.py, conv2d-cal.tflite first.
- Optional: if gahingwoo replied to gahingwoo-code-bug-found-2026-06-26.md, cross-check his layout vs ours.

## 1. IMPLEMENT — rkt_coefs.c `rkt_fill_biases`
Current: allocates `output_channels*sizeof(uint32_t)` (512B), writes only `biases[oc]`.
Change to build the 1024B (= ALIGN(output_channels,8)*8) surface in interleave-by-8:
```
records = DIV_ROUND_UP(output_channels, 8);
size    = records * 64;                       // 8 ch * 8 B
buf (uint8). For each oc:  r=oc/8; lane=oc%8;
  *(int32*)(buf + r*64 +  0 + lane*4) = bias[oc];          // corrected bias (corr=0 here)
  *(int16*)(buf + r*64 + 32 + lane*2) = (int16)shift[oc];  // = 25
  *(int16*)(buf + r*64 + 48 + lane*2) = (int16)mult[oc];   // = 32052
```
- bias[oc] = (biases_in[oc] - corr) >> truncate_bits  (keep existing corr/truncate logic; corr=0 here).
- Compute per-channel ratio: ratio[oc] = weight_scale[oc?] * input_scale / output_scale. For per-tensor
  use scalar scale; for per-axis read `poperation->conv.weight_tensor->scales[oc]` (pipe_tensor exposes
  `float *scales` — p_state.h). Get input_scale = input_tensors[0]->scale, output_scale =
  output_tensors[0]->scale (add these reads; they're on pipe_tensor).
- Fixed-point helper: normalize ratio→(mult16, shift6): pick shift so mult = round(ratio*2^shift) in
  [2^15,2^16); clamp shift≤63. (Sanity: ratio 9.55e-4 → mult≈32052, shift≈25.)
- Remove the inert BIASPROBE hack here (rkt_coefs.c.bak-biasprobe is the pre-hack original).

## 2. IMPLEMENT — rkt_regcmd.c BS block (~line 282) + RDMA (~line 504)
- BS_CFG: keep BS_ALU_ALGO(2) | BS_ALU_SRC(1) | BS_RELU_BYPASS(1); **DROP BS_MUL_BYPASS**.
- BS_MUL_CFG (was 0): set **BS_MUL_SRC(1) | BS_TRUNCATE_SRC(1)** (operand+shift stream from buffer).
- BRDMA_CFG (line 504): widen BRDMA_DATA_USE so it fetches bias+scale (try the "both" enum; if a 4-bit
  mask, bias+mul bits). RDMA_BS_BASE_ADDR already points at the (now 1024B) buffer.
- Remove the inert STEP-1 register-requant block (rkt_regcmd.c.bak-step1 is the pre-STEP1 original).
- Leave CNA CVT bypass as-is for multi-channel input (requant now lives in SDP BS_MUL).

## 3. BUILD + TEST
- Build libteflon.so (background ~3min). Deploy to board's mesa install path used by test_conv.py.
- Re-scp `for_review/from_gahingwoo/{test_conv.py, conv2d-cal.tflite}` to /tmp.
- ONE job: `TEFLON_LIB=<libteflon.so> python3 /tmp/test_conv.py /tmp/conv2d-cal.tflite` on PRIMED rocket.
- **Judge by maxdiff** (distinct≫2 = computing; maxdiff small vs CPU = WIN). Do NOT read perf counters.

## 4. If degenerate still — disambiguate (one lever at a time, ≤2 jobs/burst)
- SDP order ambiguity: if out wrong by a constant scale, try bias in OUTPUT domain (bias*ratio) i.e.
  order acc*mult+bias instead of (acc+bias)*mult.
- colA/colB swap: try shift in colB / mult in colA position (we inferred from field widths, but verify).
- BRDMA_DATA_USE value: sweep the enum (it gates whether MUL operand is actually fetched).
- Weight-zp bug: rkt_fill_weights subtracts 0x80 not wzp=133 — fix to `- weight_zero_point` and refold
  bias correction (corr uses input_zp−0x80 which is 0 here, so weight-zp error is uncorrected in acc).
- Capture vendor regcmd BS_CFG/BRDMA_CFG (kernel-PA route, heavier) to read the exact enum/global shift.
- Last resort: feed our buffer = vendor-bias.bin first 1024B directly (gahingwoo A/B) to prove the
  consumption path before trusting our value computation.

## 5. Validate + write up
- maxdiff small → first correct inference on rocket. Then test a genuinely per-axis model to exercise
  per-channel mult/shift. Then draft mesa MR + update gahingwoo/Tomeu.

## Artifacts / staging
- Decode scripts + weights.npy/bias.npy: session scratchpad (regenerate via gt_dump.py on board if gone).
- mesa originals: rkt_coefs.c.bak-biasprobe-*, rkt_regcmd.c.bak-step1-* on board (revert hacks from these).
- vendor-bias.bin coefficient region = first 1024B (decode confirmed); rest = packed weights (ignore).
