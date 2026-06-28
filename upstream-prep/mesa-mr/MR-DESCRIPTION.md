# rocket/Teflon: RK3568 NPU enablement (Draft — for collaboration)

**Suggested MR title:**
`rocket/teflon: add RK3568 NPU support (TRM-derived conv config fixes) [Draft]`

**Labels:** `Draft`, component `gallium/rocket` (Teflon)
**cc:** Tomeu Vizoso (@tomeu) — opened at his request to collaborate on RK3568
bring-up; he has no RK3568 hardware, I have a Radxa ROCK 3B and can run any test.

---

## Summary

The Rocket Gallium driver / Teflon delegate currently targets the RK3588 NPU.
The RK3568 NPU is the same NVDLA-derived IP but differs in CBUF size, several
register layouts, and a number of encoded config values that Mesa currently
hard-codes to RK3588-appropriate values.

This MR collects the **TRM-derived and BSP-verified** config fixes needed to
drive the RK3568 NPU through `rkt_regcmd.c`. With these changes applied, on a
Radxa ROCK 3B (RK3568) the pipeline activates end-to-end: the PC command
processor fetches and broadcasts the regcmd, the CNA enters its operating
state, and (with the matching kernel RFC series applied) interrupts fire.

**This is intentionally a Draft.** It is **not** correct yet: a `conv2d.tflite`
run through Teflon still produces wrong output (every element is the output
zero-point). There are two distinct, independently-confirmed causes (see
"Known limitations"):

1. **Mesa-side:** some conv config values are still RK3588-specific and wrong
   for RK3568 (verified by a byte-exact diff against the vendor command list for
   the *same* convolution).
2. **Hardware/driver-side:** even a **byte-exact replay of the vendor (RKNPU)
   command list** for the same conv does not complete through the mainline
   rocket kernel driver on RK3568 — the MAC/output stage never finishes. So the
   command-list content is *not* the only blocker; there is a HW/driver
   bring-up (executer-engage) gate that is being chased separately on the kernel
   side.

I'm opening this Draft so the userspace divergences are on record, the
SoC-parameterisation direction can be discussed early, and we can collaborate
on the remaining gap. The kernel side was already sent as an RFC to
dri-devel / linux-rockchip.

Hardware: Radxa ROCK 3B (RK3568). Mesa 25.3.0. Test: `conv2d.tflite` via the
Teflon delegate. Kernel: rocket with the RK3568 RFC series applied.

---

## What's included

All changes are in `src/gallium/drivers/rocket/`. They are grounded in the
RK3568 TRM Part 2 (Chapter 9, NPU, pages ~410–462) and cross-checked against a
byte-exact vendor RKNPU command-list capture for the identical convolution.

### `registers.xml`

1. **Add `CONV_CON4` (CNA, offset 0x1018) with `RGB_BYTELENGTH` field
   (bits 0–23).** This register was entirely missing from the XML (gap between
   `CONV_CON3` at 0x1014 and `DATA_SIZE0` at 0x1020). Per TRM p.418. On RK3568
   this register being non-zero is required for the CNA pipeline to start
   (empirically verified — at HW default 0 the CNA OP_ENABLE entry is rejected).

2. **Expose `FETCH_PIXEL_LEN` (bits 8–15) in `DMA_CON0` (offset 0x1078).**
   These bits were previously labelled `RESERVED_1`; TRM p.422 documents them as
   the per-surface feature fetch length. Left at 0, CNA fetches no feature data
   per surface on RK3568.

3. **Correct the `FEATURE_MODE_CFG` (DPU, offset 0x400C) bit layout** to match
   TRM p.433. The previous layout had `OUTPUT_MODE` / `CONV_MODE` / `BURST_LEN`
   at the wrong bit positions:
   - `FLYING_MODE`: bit 0
   - `OUTPUT_MODE`: bits 1–3 (was 1–2)
   - `CONV_MODE`: bits 4–5 (was 3–4)
   - `BURST_LEN`: bits 7–8 (was 5–8, which polluted CONV_MODE/reserved bits)

### `rkt_ml.h`

4. **`CBUF_BANKS` 12 → 8.** RK3568's convolution buffer is 256 KiB = 8 banks
   × 32 KiB; RK3588 has 384 KiB = 12 banks (TRM p.410). Carries a TODO to
   select this per-SoC at runtime via `rkt_device` rather than a compile-time
   constant — see "Known limitations / cleanup".

### `rkt_regcmd.c` (`fill_first_regcmd`)

5. **Emit `CNA_CONV_CON4`** with `RGB_BYTELENGTH = input_w * input_h *
   input_channels` (a safe non-zero per-task input byte count). Required to
   start the CNA pipeline on RK3568. *Caveat:* the byte-exact vendor diff shows
   the vendor writes **0** here for our specific conv, so this value is still
   not believed correct for general convs — see "Known limitations".

6. **Add `KERNEL_GROUP` to the `CONV_CON2` emit** = `weights_kernels / 32 - 1`
   (int8 mode: 32 kernels per group, TRM p.418), and replace the previous
   `FEATURE_GRAINS` "+50" magic with the TRM-suggested formula
   `stride_y + weights_height + 1`. *Caveat:* the byte-exact diff disagrees with
   both the old and new `CONV_CON2` value for our conv (vendor 0x70 vs Mesa
   0x30080), so this formula is still suspect.

7. **Add `FETCH_PIXEL_LEN(input_width)` to the `DMA_CON0` emit** (was 0).

8. **Change `DPU_FEATURE_MODE_CFG` to `BURST_LEN(2) | OUTPUT_MODE(4)`**
   (= 0x108), matching the vendor value exactly. The TRM only documents
   `OUTPUT_MODE` codes 0 (PPU) and 2 (outside); the vendor uses 4 in practice,
   so this is empirical.

9. **Emit all five sub-unit `S_POINTER` wakes contiguously, before CNA config**
   (CNA, CMAC, ACCU/CORE, DPU, DPU_RDMA). Mesa previously only woke DPU and
   DPU_RDMA and interleaved them between CNA configs; the vendor wakes all five
   first. *Note:* CMAC (routing target 0x400) is not in Mesa's `registers.xml`
   target enum, so this currently uses an `emit_raw(regs, 0x401, 0x2004, 0xe)`
   workaround — see "cleanup".

10. **Use the RK3568 DCOMP descriptor layout** (8 ADDR at 0x1110–0x112c + 8
    AMOUNT at 0x1130–0x114c) instead of RK3588's (1 ADDR + 16 AMOUNT at 0x1140+),
    per the BSP regcmd capture. Decompression stays disabled (all AMOUNTs 0,
    ADDR0 = weight base). Currently via `emit_raw` — see "cleanup".

11. **Replace the RK3588 broadcast OP_ENABLE (target 0x81) with individual
    sub-unit OP_EN writes.** The 0x81 broadcast target is RK3588-specific and
    not supported on RK3568; direct per-unit writes work on both. (This already
    had one RK3568 reference in tree; this MR keeps and relies on it.)

---

## Known limitations

This MR is **not** ready to un-Draft. Two separate problems remain, both
honestly reproduced and characterised:

### A. Some conv config values are still RK3588-specific (Mesa-side)

A byte-exact diff against the vendor RKNPU command list for the **same**
convolution (`conv2d`, 80×80×16 → 40×40×128, 5×5 stride 2, int8) shows Mesa
still emits several wrong values for RK3568. The clearest:

| offset | reg | Mesa | vendor | note |
|--------|-----|------|--------|------|
| 0x1010 | CONV_CON2 | 0x00030080 | 0x00000070 | kernel_group/feature_grains formula wrong for RK3568 |
| 0x1018 | CONV_CON4 | 0x00019000 | 0x00000000 | vendor writes 0 for this conv (our non-zero value is needed only to pass the pipeline-start gate) |
| 0x107c | DMA_CON1 line_stride | 0x140 (320) | 0x50 (80) | 4× too large (input width is 80) |
| 0x1044 | CBUF_CON1 | 0x00000014 | 0x00500028 | |
| 0x1078 | DMA_CON0 | 0x000f500f | 0x00171c07 | wt_burst / fetch_pixel / data_burst |
| 0x1080 | DMA_CON2 surf_stride | 0x17c0 | 0x1900 | |

Plus several DPU/ACCU/DPU_RDMA values (e.g. 0x40c0 Mesa 0xc800 vs vendor 0x6400
— a 2× factor suggesting an element-vs-byte or channel-grouping miscount). These
are real per-SoC value bugs that the eventual correct MR will fix in the
`rkt_regcmd.c` value formulas. They are *not* yet fixed here because of (B).

### B. A hardware/driver compute-completion gate (kernel-side, not the command list)

The decisive finding: **replaying the vendor's byte-exact command list for the
same conv through the mainline rocket *kernel* driver on RK3568 also does not
complete.** Verified state during the (failing) run:

- The CNA **reads** the full input and weights (NPU DMA bandwidth counters
  feature-read / weight-read are non-zero).
- The **MAC/output stage never completes**: DPU feature-write counter = 0, no
  completion IRQ, the `drm_sched` job times out (~540 ms), output stays at the
  buffer zero-point.
- All five sub-units sit at `S_STATUS = 0x1` ("executer 0 operating") for the
  whole timeout; none advances. Driving rocket to the closest-to-vendor state,
  the entire remaining diff is `S_POINTER` bit 16 (0x10000), a **HW-set
  "executer engage" bit** that is set on the BSP and never on mainline.

Because **identical instructions + identical config + identical IOVAs produce
opposite results** on BSP vs mainline kernel, this gate is in the
driver-init/submit-time HW bring-up, **not** in anything Mesa can put in the
command list. Overlaying ~58 vendor register values onto Mesa's command list
did not fire execution either. This is being chased on the kernel side and is
the main thing collaboration would help with.

Net effect: even once the Mesa value bugs in (A) are fixed, correct output is
not expected until (B) is resolved. That is why this is a Draft.

### Cleanup needed before un-Drafting (see COMMITS.md)

- CBUF banks / DCOMP layout should be selected **per-SoC at runtime** (via
  `rkt_device` / `screen`), not by a compile-time `#define` and `emit_raw`
  hacks; RK3588 must not regress.
- Add a proper `CMAC` (0x400) target + domain to `registers.xml` and drop the
  `emit_raw(0x401, ...)` workaround.
- Replace the remaining `emit_raw(...)` DCOMP block with named-register emits
  once the RK3568 DCOMP registers are in the XML.
- Replace empirical magic (`OUTPUT_MODE(4)`, the CONV_CON4 non-zero gate value)
  with documented/derived values once understood.

---

## How to test

Hardware: Radxa ROCK 3B (RK3568). Kernel: rocket driver with the RK3568 RFC
series applied (boot entry `l3` in this project), `/dev/accel/accel0` present.

Build:

```sh
cd /path/to/mesa
git apply rocket-rk3568-trm-fixes.patch   # (or the squashed commit in this MR)
meson setup build -Dgallium-drivers=... -Dteflon=true   # match your config
ninja -C build
# deploy build/src/gallium/targets/teflon/libteflon.so to the board
```

CPU reference (no delegate), expected `sum = 23252430`:

```python
import tflite_runtime.interpreter as tflite
import numpy as np
m = 'src/gallium/targets/teflon/tests/conv2d.tflite'
it = tflite.Interpreter(model_path=m); it.allocate_tensors()
i = it.get_input_details(); o = it.get_output_details()
it.set_tensor(i[0]['index'], np.ones(i[0]['shape'], dtype=i[0]['dtype']))
it.invoke()
print('CPU sum:', int(it.get_tensor(o[0]['index']).sum()))   # 23252430
```

Rocket delegate (current, **wrong**), observed `sum = 25866240`:

```python
import tflite_runtime.interpreter as tflite
import numpy as np
d = tflite.load_delegate('build/src/gallium/targets/teflon/libteflon.so')
m = 'src/gallium/targets/teflon/tests/conv2d.tflite'
it = tflite.Interpreter(model_path=m, experimental_delegates=[d]); it.allocate_tensors()
i = it.get_input_details(); o = it.get_output_details()
it.set_tensor(i[0]['index'], np.ones(i[0]['shape'], dtype=i[0]['dtype']))
it.invoke()
print('Rocket sum:', int(it.get_tensor(o[0]['index']).sum()))  # 25866240 (wrong)
```

| | sum | output values |
|---|---|---|
| Expected (CPU TFLite) | 23,252,430 | varied across channels |
| Actual (rocket, this MR) | 25,866,240 | uniform output zero-point (every element = 128) |

Notable: the rocket output is also **byte-identical for inputs 0 / 1 / 128 /
255**, confirming the convolution never actually executes (output is the
bias/zero-point path only) — consistent with the limitation (B) above.

Conv2d test shape, for reference: input 80×80×16, output 40×40×128, kernel 5×5,
stride 2, int8 (input/output zero-point = 128).

**No RK3588 regression expected:** `CONV_CON4 = 0` was the old behaviour on
RK3588; `DPU_FEATURE_MODE_CFG = 0x108` matches what RK3588 already needs; the
XML `FEATURE_MODE_CFG` bit-position fix corrects a layout that produced junk
bits. The one risky change is `CBUF_BANKS` (compile-time 12 → 8); the cleanup
above (per-SoC selection) must land before this is mergeable so RK3588 keeps 12.
I cannot verify RK3588 myself — that's one thing I'd ask for help testing.

---

## References

- RK3568 TRM Part 2, Chapter 9 (NPU), pages ~410–462 — register field
  citations above (CONV_CON4 p.418, DMA_CON0 p.422, FEATURE_MODE_CFG p.433,
  CBUF p.410).
- Kernel RFC (RK3568 rocket support): lore.kernel.org/linux-rockchip
  (`?q=Add+RK3568+NPU+support`).
- Byte-exact vendor-vs-Mesa command-list diff and the compute-gate
  characterisation are available on request (BSP RKNPU regcmd captures + diff
  tooling).
