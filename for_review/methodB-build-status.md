# Method B — rocket-side replay submitter: BUILT (2026-06-22). Next: run on l3.

## Deliverables (this session)
- **`rocket_replay.c`** — rocket-side replay submitter. Compiles CLEAN (gcc -Wall -Wextra, x86 local
  syntax/type check). Self-contained (rocket UABI structs+ioctls inline, no libdrm dep). Flow:
  open /dev/accel/accel0 → CREATE_BO {input,weight,output,rdma-scratch,regcmd} → mmap+fill →
  patch the 4 address slots (0x1070→input, 0x1110→weight, 0x4020→output, 0x5020→rdma) to our BO
  dma_addrs → FINI_BO all → SUBMIT (1 job, 1 task, regcmd=rcbo.dma, count=133) → PREP_BO(output,2s)
  → read output, print distinct/sum, diff vs golden.
- **`regcmd_from_dump.py`** — reconstructs regcmd.bin from the REGCMD_FULL_BSP text dump.
  64-bit word = (routing<<56)|(1<<48)|((val&0xffffffff)<<16)|(off&0xffff). Surfaced that only
  0x1070/0x1110/0x4020/0x5020 are real addresses; 0x4054=0xfffffffb and 0x4080=0xffffff80 are CONFIG
  constants (do NOT patch).
- **`rknn_raw_run.c`** — rknn golden runner (RKNN_HOLD=1 keeps BOs alive for /dev/mem dump).
- Payload in `for_review/`: `conv2d-regcmd.bin` (1064B/133 words), `conv2d-weights.bin` (51200B),
  `conv2d-golden-out.bin` (204800B), `conv2d-bsp-cmdstream.txt`.

## Golden contrast (the test)
Vendor (rknn, l1) computes our exact conv2d: out distinct=24. rocket: distinct=2 (zero-point).
If rocket_replay (byte-exact vendor cmdstream + weights + input through the rocket kernel UABI) also
gives distinct=2 → the engage gate is ISOLATED to the rocket kernel submission path (mesa eliminated).

## ▶ STEP 3 RUN PLAN (next session, needs board on l3, reachable)
1. Boot l3 (rocket). Ensure rocket loaded with DEFAULT params (no head_arm/cbuf_con0/BSP_INJECT/etc.);
   consider `desc_mode=0` so the submit path is direct regcmd, not task-descriptor-wrapped.
2. scp rocket_replay.c + conv2d-regcmd.bin + conv2d-weights.bin + conv2d-golden-out.bin to board.
3. `gcc rocket_replay.c -o rocket_replay` (aarch64, native — only local x86 syntax-check done so far).
4. `./rocket_replay conv2d-regcmd.bin conv2d-weights.bin conv2d-golden-out.bin`
5. Read: distinct (>2 = engaged!), diff vs golden, + dmesg engage signature (AFTER_OPEN_ENGAGE bit16,
   ALL_SP S_STATUS, PC TASKST 0xf000 vs 0x11000).

## OPEN VALIDATIONS before trusting a NEGATIVE result
- **regcmd.bin byte-exactness**: the (1<<48) bit is an assumption from the odd-word=0x02010000 pattern.
  VALIDATE: on l1, RKNN_HOLD=1 run + /dev/mem dump 1064B from the regcmd PA (printed in REGCMD_FULL_BSP)
  and diff vs conv2d-regcmd.bin. (Tried this session; missed the 90s hold window when office connectivity
  dropped — retry.)
- **weights.bin**: from conv2d-bsp.rknn @0x1bc0 (NPU-format, 256B verified vs runtime earlier). For
  byte-exact runtime weights, /dev/mem dump the weight BO (IOVA 0xfffe0000) on l1.
- **rdma scratch (0x5020)**: size/content unknown; allocated 256KB zeroed. Fine for an ENGAGE test
  (engage doesn't depend on rdma content); may affect output correctness only.
- **rocket default path**: confirm rocket_job.c default (no params) passes our regcmd through unmodified
  (the inject/patch logic is param-gated; desc_mode default=1 wraps a descriptor — try desc_mode=0).
- **task.regcmd is u32**: rocket BO dma must be < 4GB (rocket assigns low IOVAs historically — OK;
  submitter WARNs if >32-bit).

## Interpretation matrix (step 3)
- distinct=2, TASKST=0xf000  → gate = rocket KERNEL driver submission (mesa + payload eliminated). Decisive.
- distinct>2 / matches golden → rocket kernel driver is fine; gate was in mesa userspace (regcmd gen).
- ioctl error / fault       → submitter/UABI bug; fix and rerun.
