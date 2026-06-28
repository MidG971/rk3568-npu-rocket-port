---
name: firstconv-weight-packing-bug-2026-06-17
description: gahingwoo localized RK3576 conv0 zero-output to a mesa firstconv weight-buffer LAYOUT bug (RK3588 generic packing vs the CNA's firstconv ARGB layout) — prime suspect for OUR RK3568 uniform-128 output once units run
metadata:
  type: project
---

**gahingwoo 2026-06-17 (3rd update): RK3576 conv0 zero-output narrowed to a MESA WEIGHT-PACKING bug.**
This supersedes the "CBUF execution state needs soft-reset" theory (now disproven).

**Soft-reset theory DEAD (but cleared a path):** he added CBUF resets (`SRST_A_RKNN_CBUF` +
`SRST_H_RKNN_CBUF`) and ran the vendor full soft-reset (assert all NPU resets → deassert → re-attach
IOMMU → state_init) ONCE per power-up, before any domain attach → conv0 STILL `distinct=2`. So CBUF
does NOT need the soft-reset to compute. `OUT_CVT` shift sweep (0/+7/+25) → `distinct=2` always, so
requant isn't collapsing a real sum either.

**✅ IOMMU reset-survival SOLVED by SEQUENCING (validates our RFC v3, NOT my DTE-patch lead):** the
vendor soft-reset's IOMMU re-attach survives cleanly on MAINLINE rockchip-iommu — no `MMU_DTE_ADDR is
not functioning`, no fault — **because it's done reset-before-attach, once per power-up, not per-job
mid-operation.** The earlier `-14` breakage was a per-job reset corrupting a LIVE attachment. ⇒
strongly corroborates our **attach-once + reset-before-IOMMU-detach** design (already in RFC v3). My
"disable fetch dte time limit" lead was the wrong fix — the issue was reset TIMING, not DTE fetch.

**THE BUG: firstconv weight buffer wrong size + format (mesa userspace).**
- conv0 (NOT depthwise) takes mesa's **generic RK3588 weight-packing path** → produces an
  **18432-byte** weight buffer (`3*3*32*ALIGN(16,32)*2`).
- But the CNA weight DMA (`REG 0x101c`) is programmed to read **1536 bytes** (`0x600`) in the
  **firstconv ARGB layout** (32 oc × 48; 48 = 4 ARGB channels × 3×3 spatial, padded).
- ⇒ CNA reads the first 1536 of 18432 bytes in the WRONG ORDER → garbage weights → MAC ≈ 0 → output
  sits on the int8 zero-point. (Also explains why overwriting the weight buffer never changed output —
  those bytes were never in firstconv format.)
- Same class as the **depthwise** weight-layout bug (broke on generic RK3588 packing; needed a
  separate RK3576 layout, derived by capturing the vendor runtime weight BO and reading back which
  byte tracks which (channel, ky, kx)). **The firstconv layer never got its own layout branch.**
- gahingwoo's next: capture vendor conv0 weight BO → exact (oc, ic, ky, kx)→byte map → add the
  firstconv packing branch in mesa.

**▶ MEASURED ON RK3568 2026-06-17 (l3): the firstconv-ARGB SIZE bug does NOT apply to our conv2d.**
conv2d = 5×5 kernel, 16 ic, 128 oc (input [1,80,80,16] → output [1,40,40,128], stride 2). Decoded
mesa's weight programming (template values, pre-patch): `WEIGHT_SIZE0=0xc800=51200` total bytes,
`WEIGHT_SIZE1=0x190=400` bytes/kernel (=5·5·16 ✓), `WEIGHT_SIZE2=0x05050080` → WIDTH=5 HEIGHT=5
KERNELS=128 ✓. 5·5·16·128=51200 ✓. ⇒ **mesa uses the GENERIC packing path with CORRECT sizes** — NOT
the firstconv-ARGB layout (that would be ~32oc×48=1536). Our 16-ch input correctly takes the generic
path. (Live AT10MS showed `0x1030=0x180 0x1034=0x20 0x1038=0x0202000c` = OLD DEBUG PATCHES, src
~lines 892-899, NOT mesa output — ignore.) Weight BO @IOVA 0xfff80000, real int8 data
(`e9 0a c9 04 23 fc 0d da...`).

**⇒ For RK3568 conv2d, gahingwoo's firstconv weight-SIZE mismatch is RULED OUT** (sizes correct,
generic path). BUT the **byte-ORDER (tiling) bug IS CONFIRMED** — see next.

**✅ BYTE-ORDER BUG CONFIRMED 2026-06-17 (mesa runtime buf vs `conv2d-bsp.rknn`, analysis in
`for_review/weight-packing-analysis-2026-06-17.md`):** captured mesa's runtime weight BO (first 512B,
extended kernel dump → `for_review/mesa-weights-512.log`) and compared to the vendor `.rknn` weight
blob (same conv2d, blob @file-offset 0x1bc0). Findings:
- **mesa[0:256] BYTE-IDENTICAL to vendor @0x1bc0** → same raw weights, same 256-B "atom" format.
- first mismatch at **byte 256** (one atom in); mesa's atom-1 reappears in vendor at **+6144 B**
  (24 × 256-B atoms); the 6144-B gap is **99% real weight data (interleave, NOT padding)**.
- ⇒ **mesa packs correct weights in RK3588 CONTIGUOUS tiling; RK3568 CNA reads an INTERLEAVED order
  (256-B atom, 6144-B/24-atom period)** → CMAC consumes scrambled weights → wrong/zero MAC. Same CLASS
  as gahingwoo's firstconv/depthwise layout bugs, but the GENERIC conv tiling. **Pure MESA fix**
  (rkt_ml.c/rkt_resource.c weight tiling).
- Vendor data BANKED via the `.rknn` directly — no l1 boot needed; the 256-B verbatim match proves the
  `.rknn` blob is NPU-format. An l1 runtime capture would only confirm no load-time re-pack (low value).

**CAVEAT (unchanged):** DOWNSTREAM of the engage gate + UNVERIFIABLE until we engage. Our current
uniform-128 is from NON-ENGAGE (DPU never writes), NOT these scrambled weights. This is the **#1
output-correctness suspect for when we engage**, banked as a mesa-report item. Engage gate
([[ppgroup-parity-reframe-2026-06-17]]) remains THE blocker. See [[gahingwoo-cbuf-iommu-wall-2026-06-17]].

**Ask to gahingwoo:** share the vendor conv0 (oc,ic,ky,kx)→byte map / firstconv packing formula once
captured — same mesa code path, directly reusable for RK3568. Plus `top wt_rd` offset (still open).
