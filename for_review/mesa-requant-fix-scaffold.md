# Scaffold: per-channel int8 requant fix for mesa rocket (2026-06-26)

Bug: mesa rocket does no per-channel requant → int8 per-axis conv outputs zero-point rail
([[mesa-per-channel-requant-bug-2026-06-26]]). This scaffolds the fix; the one PENDING piece (exact
per-channel coefficient-buffer layout) comes from gahingwoo + the allbilly-repo agent.

## The three change sites (all in src/gallium/drivers/rocket/)
1. `rkt_regcmd.c:282-287` — DPU BS_CFG: `BS_MUL_BYPASS(1)` → un-bypass + configure the multiply.
2. `rkt_regcmd.c:285-286` — DPU BS_MUL_CFG / BS_ALU_CFG: currently 0; set the requant operand(s).
3. `rkt_coefs.c rkt_fill_biases` — currently biases-only; add per-channel scale (mult+shift) to the
   coefficient buffer (for the BS_MUL_SRC=1 per-channel path).

## Register semantics (from registers.xml)
- `BS_CFG` (0x4040): BS_MUL_BYPASS (pos4), BS_ALU_SRC (pos8), BS_ALU_ALGO (16:19), BS_BYPASS(pos0),
  BS_ALU_BYPASS(pos1).
- `BS_ALU_CFG` (0x4044): BS_ALU_OPERAND (0:31) — single bias operand when ALU_SRC=register.
- `BS_MUL_CFG` (0x4048): BS_MUL_OPERAND (16:31, 16-bit mult), BS_MUL_SHIFT_VALUE (8:13, 6-bit shift),
  BS_MUL_SRC (pos0: 0=register/per-tensor, 1=buffer/per-channel), BS_MUL_SHIFT_VALUE_NEG (4:9).
- mesa today: BS_ALU_SRC=1 (bias from per-channel buffer — WORKS), BS_MUL_BYPASS=1 (multiply OFF).

## The derivable math (all inputs available via pipe_tensor)
Per output channel c:
  ratio[c] = weight_tensor->scales[c] * input_tensors[0]->scale / output_tensors[0]->scale
(For per-tensor models without ->scales, fall back to ->scale.) Then fixed-point:
  pick shift s.t. mult = round(ratio[c] * 2^shift) fits in 16 bits (BS_MUL_OPERAND); shift in 6 bits.
NVDLA SDP applies: out = sat8( ((acc + bias[c]) * mult[c]) >> shift[c] + output_zero_point ).

## STEP 1 (small, testable first): per-TENSOR requant via register (no buffer)
In rkt_regcmd.c, replace the BS block:
```
unsigned mult, shift;
rkt_requant_fixed(weight_scale * input_scale / output_scale, &mult, &shift); /* helper to add */
EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                     DPU_BS_CFG_BS_RELU_BYPASS(1));            /* BS_MUL_BYPASS dropped */
EMIT(REG_DPU_BS_ALU_CFG, 0);
EMIT(REG_DPU_BS_MUL_CFG, DPU_BS_MUL_CFG_BS_MUL_OPERAND(mult) |
                         DPU_BS_MUL_CFG_BS_MUL_SHIFT_VALUE(shift) |
                         DPU_BS_MUL_CFG_BS_MUL_SRC(0));        /* 0 = per-tensor register */
```
Uses the SCALAR weight scale → won't be exactly right for a per-axis model (varied per-ch scales), but
SHOULD move the output off the zero-rail (distinct>1) and proves the BS_MUL path. Judge with
test_conv.py maxdiff (NOT perf counters). If distinct>1 → BS_MUL is the lever, proceed to STEP 2.

## STEP 2 (full fix): per-CHANNEL via buffer (BS_MUL_SRC=1)  — PENDING layout
- Set BS_MUL_SRC=1 in BS_MUL_CFG.
- In rkt_coefs.c, extend the coefficient buffer to carry per-channel {bias, mult, shift} (or per-ch
  float scale — vendor-bias.bin has FLOAT32 per-ch scales, so the SDP may consume float, not fixed
  mult+shift). **PENDING from gahingwoo / allbilly repo:** (a) one channel's record format
  (offsets/types), (b) float-vs-fixed, (c) interleave/order, (d) does BS_MUL_SRC=1 read from the same
  BO as BS_ALU bias or a separate coeff BO + which address reg points at it.
- Match vendor-bias.bin byte-for-byte for conv2d-cal, then maxdiff.

## Validation
intercept.c already dumps `biases%d.bin` from the regcmd; ROCKET_DBG_DUMP_BOS dumps biases/weights.
Capture mesa's current coeff buffer + diff vs vendor-bias.bin to confirm the format gap before STEP 2.
Build on LXC <COMPILE-SERVER-IP> (fast); deploy libteflon.so; run test_conv.py on PRIMED rocket (l8).
RULE: output maxdiff only; ≤2 NPU jobs/burst (wedge); SysRq s-u-b → l8 auto-reprime.
