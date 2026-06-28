# RK3568 conv2d weight-packing analysis — mesa vs vendor (2026-06-17)

Goal: test gahingwoo's "the weight BUFFER the CNA reads can be in RK3588 order the CMAC can't use"
hypothesis on our RK3568 conv2d, by comparing mesa's runtime weight buffer to the vendor's packing.

## Inputs
- `for_review/mesa-weights-512.log` — mesa's runtime weight BO, first 512 bytes (captured on l3 via
  extended kernel WEIGHT_BO dump; conv2d.tflite, IOVA 0xfff40000).
- `for_review/conv2d-bsp.rknn` — vendor toolchain output for the SAME conv2d (59360 B; weight blob
  starts at file offset 0x1bc0).
- conv2d: 5×5 kernel, 16 input ch, 128 output ch → 51200 weight bytes, 400 B per output-channel kernel.

## Result: SAME weights, DIFFERENT tiling order (byte-order bug CONFIRMED)
- mesa[0:256] is **byte-identical** to vendor weight blob @0x1bc0 → same raw weights, same 256-byte
  "atom" format for the first atom.
- mesa[0:512] is NOT contiguous in the vendor blob; first mismatch at **byte 256** (one atom in).
- mesa's atom-1 (bytes 256..) reappears in the vendor blob at **+6144 bytes** (0x34c0).
- The 6144-byte gap between is **99% nonzero real weight data** (not padding) → the vendor
  **interleaves** 256-byte atoms with a period of **6144 B = 24 atoms**, whereas mesa lays atoms
  **contiguously**.

⇒ mesa packs the correct weights in an **RK3588 contiguous tiling**; the RK3568 CNA reads weights in
an **interleaved order (256-B atom, 6144-B / 24-atom period)**. The CMAC therefore consumes scrambled
weights → MAC produces wrong/zero results. Same bug CLASS as gahingwoo's firstconv/depthwise layout
bugs, but for the GENERIC conv path (our weight SIZE programming was already correct — see
[[firstconv-weight-packing-bug-2026-06-17]]; only the atom interleave order is wrong).

## Numbers to derive the mesa fix (later)
- atom = 256 B (= 16×16; NVDLA weight atomic).
- vendor interleave period = 6144 B = 24 × 256-B atoms.
- per-output-channel kernel = 400 B (5×5×16); 128 oc; total 51200 B.
- Full derivation needs mesa's full 51200-B buffer + the vendor blob (have the .rknn) to map
  atom k (mesa contiguous) → vendor offset. 24-atom period is the key clue.

## Caveats / status
- This is DOWNSTREAM of the RK3568 engage gate (units don't run yet) and UNVERIFIABLE until we engage
  — our uniform-128 output is currently from NON-engage (DPU never writes), not from these scrambled
  weights. This is a banked mesa-report finding + the #1 output suspect for when we engage.
- .rknn-vs-runtime assumption: the .rknn weight blob is in NPU-readable format (the 256-B verbatim
  match with mesa's actual runtime buffer confirms the first atom is NPU-format). An l1 runtime
  capture would only confirm no load-time re-pack — low marginal value given this result.
- Fix is purely USERSPACE/mesa (rkt_ml.c / rkt_resource.c weight tiling), separate from the kernel
  driver.
