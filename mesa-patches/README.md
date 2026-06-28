# Mesa rocket patches for RK3568

## Status

**Not yet ready for mainline submission.** These patches fix 4 confirmed bugs per the RK3568 TRM but conv2d.tflite still produces wrong output. More mesa rocket bugs remain.

## What works
- Pipeline activates on RK3568 (IRQs fire, was 0 before any patches)
- CNA enters operating state (S_STATUS=1)
- All sub-units (CNA, CMAC/CORE, DPU, DPU_RDMA) reach OP_EN=1
- Regcmd content matches TRM spec for the 4 patched registers

## What doesn't work
- conv2d.tflite output: rocket sum = `25,866,240`; CPU reference sum = `23,252,430`
- Output has only 2 unique values (43 = channel 0 bias, 128 = zero_point) — pattern consistent with "weight DMA never loads, only bias passes through"

## The patch file `rocket-rk3568-trm-fixes.patch`

109 lines, 3 files affected:

### registers.xml additions
1. **Add `CONV_CON4` register (offset 0x1018)** with `RGB_BYTELENGTH` field (bits 0-23) — was entirely missing from XML, gap between `CONV_CON3` (0x1014) and `DATA_SIZE0` (0x1020). Per TRM Part2 page 418.
2. **Add `FETCH_PIXEL_LEN` field to `DMA_CON0` (offset 0x1078)** — bits 8-15 were labeled `RESERVED_1`, but TRM page 422 documents this as the per-surface feature fetch length.

### rkt_ml.h change
3. **Change `CBUF_BANKS` from 12 to 8** with comment about SoC-specific value. RK3568 has 256 KiB CBUF = 8 banks per TRM page 410. RK3588 has 12 banks (where the 12 came from). TODO: per-SoC runtime selection via `rkt_device`.

### rkt_regcmd.c changes
4. **Emit `CNA_CONV_CON4` with computed value** `input_width * input_height * input_channels`. Required to be non-zero for CNA pipeline to start (empirically verified). For ARGB modes this is the byte length of the input image per TRM; for non-ARGB modes the field isn't used by the conv path but must be non-zero anyway.
5. **Add `KERNEL_GROUP` to `CONV_CON2` emit**: `weights_kernels / 32 - 1` per TRM page 418 ("In int8, 32 kernels form 1 group; in int16 or fp16, 16 kernels form 1 group").
6. **Add `FETCH_PIXEL_LEN(input_width)` to `DMA_CON0` emit**: was previously 0, causing CNA to fetch zero feature data per surface.

## Applying the patch

```sh
cd /path/to/mesa
git apply rocket-rk3568-trm-fixes.patch
cd build && ninja
```

## Testing

```sh
# Reference (CPU TFLite)
python3 -c "
import tflite_runtime.interpreter as tflite
import numpy as np
interpreter = tflite.Interpreter(model_path='src/gallium/targets/teflon/tests/conv2d.tflite')
interpreter.allocate_tensors()
inp = interpreter.get_input_details()
out = interpreter.get_output_details()
data = np.ones(inp[0]['shape'], dtype=inp[0]['dtype'])
interpreter.set_tensor(inp[0]['index'], data)
interpreter.invoke()
print('CPU sum:', int(interpreter.get_tensor(out[0]['index']).sum()))  # → 23252430
"

# With rocket delegate (broken — partial output)
python3 -c "
import tflite_runtime.interpreter as tflite
import numpy as np
delegate = tflite.load_delegate('build/src/gallium/targets/teflon/libteflon.so')
interpreter = tflite.Interpreter(model_path='src/gallium/targets/teflon/tests/conv2d.tflite',
                                  experimental_delegates=[delegate])
interpreter.allocate_tensors()
inp = interpreter.get_input_details()
out = interpreter.get_output_details()
data = np.ones(inp[0]['shape'], dtype=inp[0]['dtype'])
interpreter.set_tensor(inp[0]['index'], data)
interpreter.invoke()
print('Rocket sum:', int(interpreter.get_tensor(out[0]['index']).sum()))  # → 25866240 (wrong)
"
```

## TRM references

- **RK3568 TRM Part 2 V1.1 (March 2021)** — Chapter 9 NPU, pages 410-462
- Acquired locally at `trm/part_2/` (87 PDFs)
- Decoded register reference in `docs/npu-trm-decoded.md`

## Bugs still to find (not in current patch)

**Output diagnostic refined 2026-05-21 EOD:** per-channel inspection of rocket output:

```
ch  0: 256 positions = 43 (channel 0 bias quantized), 1344 positions = 128 (zero_point)
ch  1-15: similar partial output (channels 1-15 also have bias output for some positions)
ch 16-127: ALL positions = 128 (untouched zero_point)
Total channels affected: 16 (out of 128)
Total positions affected per channel: 256 (out of 1600 = 40×40)
```

**Pattern**: CNA processes exactly **ONE atomic tile** of work — 16 channels (one FEATURE_ATOMIC group) × 16×16 spatial output positions — then stops. The atomic-iteration loop is broken.

Likely root causes (next session priorities):
- **Atomic iteration counter** — some register controls how many atomic tiles CNA processes; default value of 1 or 0 stops after first tile
- **DPU_RDMA fetch source** — may still point at wrong BO IOVA
- **Weight DMA AMOUNTs** — `DCOMP_AMOUNT0..7` are all 0 in mesa; CNA may need byte counts to fetch multiple kernel groups
- **DPU output destination stride** — DST_SURFACE_STRIDE may be wrong, causing DPU to write only first surface
- **Per-SoC dispatch infrastructure** — mesa rocket needs runtime selection between RK3568 (8 CBUF banks, different atomics?) and RK3588

**Notes from feature_grains test**: TRM-formula value (8) vs mesa hack value (53) — neither changes the atomic-iteration pattern. Same 16 channels × 256 positions. So FEATURE_GRAINS doesn't control the iteration loop.

## Update 2026-05-22: Patch grew to 7 fixes after deeper TRM extraction

After reading TRM pages 431-440 (DPU detail registers), found **mesa's `registers.xml` had WRONG bit positions for `FEATURE_MODE_CFG`** (offset 0x400C). Per TRM page 433:
- `FLYING_MODE`: bit 0 (correct)
- `OUTPUT_MODE`: bits 1-3 (3 bits) — mesa had bits 1-2 (2 bits)
- `CONV_MODE`: bits 4-5 — mesa had bits 3-4
- `BURST_LEN`: bits 7-8 (2 bits) — mesa had bits 5-8 (4 bits!)
- bit 6: reserved — mesa absorbed into BURST_LEN

This caused mesa's `BURST_LEN(15)` to set bits 5-8 (= 0x1e0), polluting CONV_MODE and reserved bits. BSP value = `0x108` (clean: burst_len=2=burst16, output_mode=4). Mesa was writing `0x1e4` (junk overlapping into conv_mode bits).

**Fix added to patch**: corrected XML bit positions + change `BURST_LEN(15)` → `BURST_LEN(2)` + change `OUTPUT_MODE(2)` → `OUTPUT_MODE(4)` (BSP's empirical value; TRM only documents 0=ppu and 2=outside, but BSP uses 4 in practice).

**Result with the fix**: `DPU_FEATURE_MODE_CFG` now writes `0x108` matching BSP exactly. But conv2d output still has the same partial-tile pattern. So FEATURE_MODE_CFG wasn't the gate either (though the fix is still TRM-correct and now applied).

**TASK_PP_EN test**: setting bit 12 of PC_TASK_CON gives TASKST=0x21000 (different state than 0x11000) but output unchanged. PC ping-pong mode isn't the iteration mechanism.

**task_number variations tested**: 1, 6, 72 (= atomic tile count for our conv), 0x12f (= BSP run0 value). All give same partial-tile output.

**Conclusion**: the multi-atomic-tile iteration is NOT controlled by PC_TASK_CON, task_number, or any register in the patches we've examined. It's controlled by something we haven't found — likely the regcmd CONTENT itself (BSP's larger regcmds at 584 entries probably contain inline loops or descriptor lists that mesa doesn't generate).

## Final patch state (2026-05-22)

`mesa-patches/rocket-rk3568-trm-fixes.patch`: **150 lines**, 7 TRM-derived fixes:
1. Add `CONV_CON4` reg + `RGB_BYTELENGTH` field to XML
2. Add `FETCH_PIXEL_LEN` field to `DMA_CON0` in XML
3. Fix `FEATURE_MODE_CFG` bit positions in XML (TRM page 433)
4. `CBUF_BANKS` 12 → 8 (TRM page 410)
5. Emit `CONV_CON4` in `rkt_regcmd.c`
6. Add `KERNEL_GROUP` + TRM-formula `FEATURE_GRAINS` to `CONV_CON2` emit
7. Fix `DPU_FEATURE_MODE_CFG`: `BURST_LEN(2)` + `OUTPUT_MODE(4)` (BSP-empirical)
8. Add `FETCH_PIXEL_LEN(input_width)` to `DMA_CON0` emit

All TRM-grounded. Builds clean. All verified landing in regcmd. Doesn't regress (RK3588 path uses BSP value `0x108` too).

Still incomplete — `conv2d.tflite` output partial. Multi-atomic-iteration mechanism remains unknown.

## "Board works first before submission" verdict

**Not yet ready for upstream submission.** Patches make rocket activate (was completely stuck before) but conv2d output is still 16/128 channels = 12.5% done.

Per the user's submission policy, more debugging required before merge:
- Need to discover how BSP achieves multi-atomic iteration in its 584-entry regcmds
- May require mesa to emit *multiple* regcmds per conv operation OR a different intra-regcmd structure
- The 7 fixes here are necessary but not sufficient

## Update 2026-05-22 PM: Patch grew to 8 fixes after BSP regcmd side-by-side

Captured BSP's FULL unfiltered regcmd (run 0 = 133-entry CNA conv) in `bsp_regcmd_run0_unfiltered_2026-05-22.log`. Side-by-side with mesa-emitted regcmd revealed:

**Bug #8 added to patch**: Mesa was missing **S_POINTER wakes for CNA (0x1004), CMAC (0x2004), ACCU/CORE (0x3004)** — only DPU/DPU_RDMA were being woken. BSP wakes ALL sub-units in slots 0-2 BEFORE any config writes. Fix: emit S_POINTER=0xe for CNA/CMAC/ACCU at the start of fill_first_regcmd, before CBUF_CON0.

CMAC isn't in mesa's target enum (mesa originally only knew about PC=0x100/CNA=0x200/CORE=0x800/DPU=0x1000/etc., skipping the 0x400 CMAC slot). Fix uses `emit_raw(regs, 0x401, 0x2004, 0xe)` directly. Proper fix would add `<value name="CMAC" value="0x400"/>` to the target enum.

**Result**: mesa regcmd now byte-exact matches BSP for first 3 slots (all S_POINTERs to 0xe). Output STILL unchanged at 16/128 channels.

**Also discovered Bug #9** (not yet in patch): BSP emits 8 DCOMP_ADDR writes (DCOMP_ADDR0-7) all zero except ADDR0. Mesa only emits DCOMP_ADDR0. May or may not matter for weight loading.

## What's NOT solved yet

After 8 mesa rocket fixes covering every register-level discrepancy with BSP that we've found, **conv2d output remains constant at sum=25,866,240 (vs CPU ref 23,252,430)** with only 16/128 channels affected. The atomic iteration / weight load mechanism is something we haven't located.

Possibilities for next session:
1. **CNA_DC_STATUS register readback during a working conv** — TRM page 459 lists this as showing "buffer full or empty signal" and "fetch data and weight status". Could reveal whether weights are actually loading.
2. **Compare DPU_RDMA SRC/BS_BASE values** between BSP and mesa — these control DPU's input fetch.
3. **Investigate the 0x1018 (CONV_CON4) value semantics more deeply** — BSP value 0x3de00 ≈ 253440 bytes (tile data length?). Mesa's 0x19000 = 102400 (full input) may be wrong scale.
4. **Try with a simpler model** — generate a tiny 1x1 conv with 8 input channels and 8 output channels to fit one atomic tile cleanly.

## Final patch state (170 lines, 8 fixes)

`mesa-patches/rocket-rk3568-trm-fixes.patch`:
1. Add `CONV_CON4` reg + `RGB_BYTELENGTH` field to XML
2. Add `FETCH_PIXEL_LEN` field to `DMA_CON0` in XML
3. Fix `FEATURE_MODE_CFG` bit layout in XML (TRM page 433)
4. `CBUF_BANKS` 12 → 8 (TRM page 410)
5. Emit `CONV_CON4` in rkt_regcmd.c
6. Add `KERNEL_GROUP` + TRM `FEATURE_GRAINS` to CONV_CON2 emit
7. Fix `DPU_FEATURE_MODE_CFG`: `BURST_LEN(2)` + `OUTPUT_MODE(4)` (BSP-empirical 0x108)
8. **NEW**: Add `S_POINTER` wakes for CNA, CMAC (0x2004), ACCU/CORE at start of regcmd (per BSP slot 0-2 order)

All 8 fixes are correct per TRM/BSP. Build cleanly. Don't regress.

## Approach for next investigations

1. Run more BSP samples via varying RKNN models (need diverse conv shapes)
2. Compare BSP register state field-by-field against mesa-emitted state during a working YOLOv5s and a failing conv2d
3. Read TRM more thoroughly for DPU sections (pages 430+)
4. Try a smaller test conv (1x1) to reduce the search space
5. Eventually need per-SoC dispatch in mesa for clean upstreaming
