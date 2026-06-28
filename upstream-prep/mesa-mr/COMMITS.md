# Commits for the RK3568 rocket/Teflon Draft MR

Source of changes: `mesa-patches/rocket-rk3568-trm-fixes.patch` (apply with
`git apply` from the Mesa root). The patch currently touches three files:
`src/gallium/drivers/rocket/registers.xml`, `rkt_ml.h`, `rkt_regcmd.c`.

## How to land it as commits

For a **Draft** opened just for collaboration, a single squashed commit is the
simplest starting point. The maintainer (Tomeu) asked for a Draft to
collaborate, not a polished series — so don't over-invest in splitting until the
direction (per-SoC parameterisation) is agreed.

### Option A — one commit (recommended for the initial Draft)

```
rocket/teflon: add RK3568 NPU conv config support (Draft)
```

Body: short version of the MR description's "What's included", explicitly
noting it is incomplete (compute gate + remaining value bugs), Draft, cc Tomeu.
End the commit message with:

```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
```

(Drop that trailer before un-Drafting if the project prefers a clean
authorship line; keep it while it's a collaboration Draft.)

### Option B — split when moving toward merge

Once the per-SoC direction is agreed and (B) the compute gate is resolved, split
into reviewable commits roughly:

1. `rocket: add CONV_CON4 register + RGB_BYTELENGTH to registers.xml`
2. `rocket: expose FETCH_PIXEL_LEN in DMA_CON0`
3. `rocket: fix FEATURE_MODE_CFG bit layout per RK3568 TRM`
4. `rocket: select CBUF bank count per SoC` (the runtime-selection rework, not
   the bare 12→8 #define)
5. `rocket: emit CONV_CON4 / KERNEL_GROUP / FETCH_PIXEL_LEN in conv lowering`
6. `rocket: emit all sub-unit S_POINTER wakes before CNA config`
7. `rocket: use RK3568 DCOMP descriptor layout`
8. `rocket: add CMAC target/domain and drop emit_raw workarounds`

(Each registers.xml addition should ideally land with the `rkt_regcmd.c` emit
that uses it, so the tree builds and the field is exercised.)

## Cleanup required before un-Drafting

These are tracked in the MR description's "Known limitations / cleanup" and
must be done before this can be a non-Draft merge candidate:

- [ ] **Per-SoC `CBUF_BANKS`**: replace the compile-time `12 → 8` `#define`
      with runtime selection (RK3568 = 8, RK3588 = 12) via `rkt_device` /
      screen, so RK3588 is not regressed. (Currently a hard `#define` change.)
- [ ] **Per-SoC DCOMP layout**: the RK3568 8 ADDR + 8 AMOUNT layout is currently
      emitted via `emit_raw(...)`. Add the RK3568 DCOMP registers to
      `registers.xml` and select layout per SoC; drop the raw emits.
- [ ] **CMAC target/domain**: add `<value name="CMAC" value="0x400"/>` + a
      `<domain name="CMAC">` (S_POINTER at 0x2004, OP_ENABLE at 0x200c) to
      `registers.xml`; replace `emit_raw(regs, 0x401, 0x2004, 0xe)` with a named
      `EMIT(...)`.
- [ ] **Resolve the remaining value bugs** (CONV_CON2 / CONV_CON4 / line_stride /
      CBUF_CON1 / DMA_CON0 / surf_stride / DPU 2× factors) against the
      byte-exact vendor diff — these are why output is wrong even ignoring the
      compute gate.
- [ ] **Replace empirical magic** with documented values:
      `DPU_FEATURE_MODE_CFG OUTPUT_MODE(4)` and the CONV_CON4 non-zero
      pipeline-start value (vendor uses 0 — understand the real gate).
- [ ] **RK3588 non-regression test**: confirm an RK3588 board still runs
      `conv2d.tflite` correctly with this MR applied. I have no RK3588 hardware;
      this needs a collaborator or CI.
- [ ] **Drop the `Co-Authored-By` trailer** if the project wants a single
      authorship line at merge time.
- [ ] **Fill in the kernel RFC lore link** in the commit/MR text once archived.
- [ ] **Resolve the compute gate (limitation B)** — required for correct output;
      tracked on the kernel side, not fixable in this Mesa MR alone.

## Notes

- Builds clean against Mesa 25.3.0 (`libteflon.so` builds with no errors/
  warnings) per prior sessions; re-verify against current Mesa main before
  pushing.
- Do **not** un-Draft until at least the per-SoC cleanups land and either the
  output is correct or the compute gate is clearly documented as an
  out-of-tree/kernel dependency that the maintainer accepts.
