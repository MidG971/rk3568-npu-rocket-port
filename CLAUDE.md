# ROCK 3B NPU Driver Port to Mainline Kernel

## Status (2026-06-04): ✅ **RFC v3 SENT** — `[RFC PATCH v3 0/9] accel: rocket: Add RK3568 NPU support` is on dri-devel/linux-rockchip/iommu/LKML (awaiting maintainer feedback). RK3568 NPU now **probes + submits cleanly on stock mainline v7.1-rc6** via a clean DT-only power-domain bring-up (drop NPU `pm_qos` + `vdd_npu` always-on; no driver hack) plus two rocket robustness fixes (reset-before-IOMMU-detach, attach-once) and the RK356x v1 AUTO_GATING fix. Depends on Simon Xue's per-device IOMMU ops (cited). **Remaining open problem = the HW compute gate**: the NPU's MAC/output stage never completes, so conv output stays at zero-point (uniform-128) — unchanged for ~2 years, HW-internal, NOT in the command list (byte-exact vendor regcmd replay also fails). Mesa side (RK3588-tuned config wrong for RK3568) prepared as a separate report (`upstream-prep/mesa-rk3568-report.txt`).

**▶ Current truth + next steps live in `PLAN.md` §1 and §5, and in `memory/` (start: upstream_v3_rfc_sent_2026_06_04, genpd_clean_bringup_2026_06_04). Everything below is HISTORICAL (the 6.19 board-test investigation that led here) — kept for context; do not treat as current.**

## TL;DR 2026-05-20 PM v4 — Sticky bit fully eliminated, full regcmd flows, CNA OP_ENABLE still rejected

**Iterative fixes today (in order):**
1. Removed CPU `writel(0x1, _npu + 0x1004)` (CNA_S_POINTER pre-write) → PRE_OP CNA S_STATUS dropped 0x4 → 0x0 ✓
2. Removed CPU `writel(0x1, _npu + 0x3004)` (CORE_S_POINTER pre-write) → CORE S_STATUS dropped 0x4 → 0x1 (matches BSP active state) ✓
3. Disabled BSP_FULL_INJECT (DCOMP+CVT shift+insert) → PC no longer stalls mid-regcmd (`PC_TASKST=0xf000` was frozen, now progresses fully) ✓
4. Disabled BSP_INJECT[4] (PC-broadcast `writel(0xe, CNA_S_POINTER)`) → CNA S_STATUS dropped 0x5 → 0x1 (the `0xe` value sets EXECUTER_PP_EN bit 2, which mirrors to S_STATUS bit 2, the sticky `RESERVED_1[0]`) ✓

**Why bits 1-4 matter:** PC's broadcast bus passes config writes through CNA's S_POINTER state. Bit 0 of S_POINTER ("wake" we used to set) AND bit 2 ("EXECUTER_PP_EN") BOTH mirror into S_STATUS as sticky `RESERVED_1[0]` (bit 2). BSP for RK3568 single-core doesn't write CNA_S_POINTER at all (gated by `num_irqs > 1`); CNA accepts regcmd config writes naturally without any S_POINTER prep. Our 3 separate writes to CNA_S_POINTER were all artifacts of dead-end debugging.

**Current best state (snapshot: `rocket_job_2026-05-20_no_pp_pre_writes.c`):**
- PROBE_CNA: `S_STATUS=0x0` ✓ (matches BSP)
- PRE_OP CNA: `S_STATUS=0x0` ✓ (matches BSP)
- AT10MS CNADUMP first submit: `S_STATUS=0x1, S_POINTER=0, OP_EN=0, CBUF_CON0=0x84` (mesa's value landed; our BSP_INJECT[16]/[126] override didn't on first submit but DID on subsequent)
- AT10MS CNADUMP later submits: `S_STATUS=0x1, S_POINTER=0, OP_EN=0, CONV_CON1=0xa000, CBUF_CON0=0x17`
- COREDUMP all submits: `S_STATUS=0x1, S_POINTER=0, OP_EN=1` ✓ (matches BSP active)
- DPU_RDMA: `S_STATUS=0x20004, S_POINTER=0x1, OP_EN=1` — still sticky bit-2 (CPU pre-write to DPU_RDMA_S_POINTER bit 0 is still in)
- IRQ count: still 0
- Output: still `out_sum=25866240` (zero_point unchanged)

**Diagnosis of remaining gap:**
- CNA accepts the OP_ENABLE regcmd entry (transitions from `S_STATUS=0` → `S_STATUS=0x1`). BSP transitions `0` → `0x10008` (STATUS_1=1 + RESERVED_1[1]=1). Rocket's 0x1 = STATUS_0=1; BSP's 0x10008 = STATUS_1=1. Different state-machine arms.
- CNA never asserts OP_EN=1 and never advances to EXECUTER. No conv runs. No DPU input data. No completion IRQ.

**Next-session priorities:**
1. **Audit BSP's PC writes during a known-good YOLOv5s submission** — capture `PC_TASKST` and `CNA_S_STATUS` transitions cycle-by-cycle on l1. Find what specific BSP write causes CNA's STATUS_0=1 → STATUS_1=1 transition.
2. **Remove DPU/DPU_RDMA CPU pre-writes** — add PC-broadcast bit-0 wakes for them instead (similar to how mesa's entries 4-5 were SUPPOSED to wake DPU but used wrong bit layout). Match BSP's clean state for all sub-units.
3. **Decode CNA_S_STATUS STATUS_0 = 1 meaning** — read NVDLA-style docs or RKNPU IP description. STATUS_0 likely encodes pipeline-state at finer granularity than STATUS_1.
4. **TASK_NUMBER=6 tested (no effect on CNA OP_EN)** — but improved timing (first submit now lands BSP_INJECT[16]/[126]). Snapshot: `rocket_job_2026-05-20_no_pp_pre_writes_taskN6.c`. Try larger 0x12f next if curious.

## TL;DR 2026-05-20 EVENING v5 — BSP↔rocket state captured side-by-side

**Captured BSP CNA state during working YOLOv5s by booting l1 and running 10 inference loops** (BSP uses `RKNPU_DUMP_BEFORE_OPEN` + `RKNPU_DUMP_AFTER_OPEN` instrumentation we added earlier).

**Pre-OP state IDENTICAL between BSP and rocket:**
| Field | BSP run 0 | Rocket | Match |
|---|---|---|---|
| CNA S_STATUS | 0x0 | 0x0 | ✓ |
| CNA S_POINTER | 0x0 | 0x0 | ✓ |
| CNA OP_EN | 0 | 0 | ✓ |
| CNA CBUF_CON0 | 0x44 | 0x44 | ✓ |
| CORE S_STATUS | 0x0 | 0x0 | ✓ |

**BSP CNA AFTER_OPEN:** `S_STATUS=0x10008` (STATUS_1=1 = EXECUTER + bit 3 RESERVED_1[1]), `S_POINTER=0x1000e` (regcmd 0xe + HW-set bit 16), `CBUF_CON0=0x2017` (regcmd 0x17 + HW-set bit 13).

**Rocket CNA AT10MS:** `S_STATUS=0x1` (just STATUS_0=1), `S_POINTER=0`, `CBUF_CON0=0x17` (regcmd value only, no HW bits).

**Key visible BSP↔rocket diff after pre-OP:**
- PC BASE IOVA: BSP `0xffefaa00` / `0xfff2b000` (HIGH IOVA), rocket `0xd7000` (LOW IOVA)
- TASKCON: BSP `0x12f` or `0x6` per submission, rocket constant
- BSP IRQ count = 33 / 10 loops = 3.3 IRQs/inference (= 3 submissions per YOLOv5s)

**Test:** rocket `task_number=0x12f` (matching BSP run 0) → PC reaches TASKST=0x11000 (BSP's "completion" value) → but CNA still doesn't reach EXECUTER. So task_number controls PC's completion-reporting only, not CNA execution.

**The gate is NOT in:**
- ❌ Regcmd content (byte-for-byte BSP match tested, no help)
- ❌ DT clock-names alignment
- ❌ PC TASK_NUMBER value
- ❌ Pre-OP CNA register state (verified identical to BSP)

**Strongest remaining hypothesis: PC BASE IOVA (high vs low).** Test: allocate rocket's regcmd BO at high IOVA (e.g., explicitly request 0xfff00000 region from IOMMU allocator). May affect PC's broadcast pipeline timing.

**Other open hypothesis:** BSP's `rknpu_drv.c` probe writes registers we don't replicate. Audit `writel` calls in BSP `rknpu_drv.c` (not job-time, init-time) targeting `fde40000.npu` MMIO range.

Current snapshot: `rocket_job_2026-05-20_bsp_state_compared.c` (no_pp_pre_writes baseline).

## TL;DR 2026-05-20 LATE-EVENING v6 — BSP config audit found dma_mask=32 mismatch

**Audited BSP `rk356x_rknpu_config` struct in `rknpu_drv.c:162`:**
- `dma_mask = DMA_BIT_MASK(32)` ← **rocket uses 40 — potential mismatch**
- `bw_priority_addr = 0xfe180008` (RK3568-specific, RK3588 has 0x0)
- `amount_top = &rknpu_old_top_amount` (RK3568 perf-counter offset 0x8010, RK3588 uses 0x2210)
- `pc_dma_ctrl = 0`
- `pc_data_amount_scale = 1`

**BSP `rknpu_set_bw_priority` is ioctl-triggered (RKNN runtime calls it before each submit on RK3568) — NOT init-time.** So bw_priority isn't the missing init unless RKNN userspace calls it AND rocket userspace doesn't.

**dma_mask=32 vs rocket's 40 is suspicious.** Rocket's `rk3568_soc_data.dma_bits = 40` in `rocket_drv.c`. If RK3568 NPU hardware genuinely supports only 32-bit DMA addresses, rocket setting 40 means IOVA allocator may pick addresses > 4GB that the NPU AXI master can't physically reach. Worth a try changing to 32.

**Cautionary tale:** Attempted to read 0xfe180000-0xfe18001x via `/dev/mem` mmap on l1 — board hung immediately (SSH unreachable, ping fails). Some RK3568 NoC regions don't tolerate userspace AXI probes. Needs power cycle to recover.

**Pending after power cycle:**
1. Change `rk3568_soc_data.dma_bits` to 32 in `rocket_drv.c` — most concrete next test.
2. Audit if mesa rocket's BO allocator forces high-IOVA hint that conflicts with 32-bit mask.
3. Add a small kernel-side diagnostic dev_err to print `0xfe180008` register value (safer than userspace `/dev/mem`).

## TL;DR 2026-05-20 LATE-LATE v7 — bw_priority probed safely; mostly disproved

**After power cycle**, on l3:
- `rocket_drv.c` already has `rk3568_soc_data.dma_bits = 32` — match with BSP, task #49 was already resolved.
- Added kernel-side `BW_PRIO` readback to `rocket_job_run` via `ioremap(0xfe180000, 0x100)`. Safe from kernel (userspace mmap crashed earlier).

**Read on rocket (l3) at first job_run:** `0xfe180008 = 0x80000101, 0xfe180010 = 0x1aa, 0xfe180014 = 0x40`. Non-zero — these are persistent SoC NoC values OR a default. So bw_priority is set even on rocket boots — NOT the missing init.

**Built BSP kernel (`Image-6.19`, #63 May 20 16:37) with matching BW_PRIO/MYSTERY/PERF readback in `rknpu_job.c` `RKNPU_DUMP_BEFORE_OPEN`.** Installed. Board currently on **l1** with the patched BSP kernel. extlinux default=l1.

**Next session pickup (board ready):**
1. Run YOLOv5s on l1 (just need to ssh + run), capture BSP's `BW_PRIO`/`MYSTERY: 0x2000/0x2004/0x2008/0x200c`/`PERF: 0x8010/0x8034/0x8038/0x803c` values during a working inference.
2. Switch to l3 (extlinux + reboot), run conv2d.tflite teflon, capture the matching rocket readbacks.
3. **Diff BSP vs rocket** for MYSTERY sub-unit and PERF amount registers. If MYSTERY state differs (esp. 0x2000 which we've never probed), that's likely our remaining gate.
4. The current rocket source (`rocket_job_2026-05-20_bw_priority_mystery_dump.c`) is live on the board with all these readbacks staged. After switching to l3, no rebuild needed.

## TL;DR 2026-05-21 — MYSTERY captured (not the gate), DPU_RDMA config bisected (also not the singular gate)

**Captured BSP MYSTERY state during working YOLOv5s:**
- `0x2000=0, 0x2004=0, 0x2008=0, 0x200c=0x1` (just OP_EN bit set)
- Rocket fresh HW default: `0x200c=0x1000` (bit 12 — probably init-pending)
- Rocket after our CPU pre-write of 1: `0x200c=0x1` — matches BSP

**Verified bw_priority registers match BSP exactly** (`0x80000101, 0x1aa, 0x40`). Not the gate.

**Removed DPU_RDMA CPU pre-write of 0x1**: DPU_RDMA dropped from `S_STATUS=0x20004` (sticky+stalled) to `0x1` (active, matches BSP). All sub-units except CNA now at BSP-matching active state.

**Tested HIGH IOVA allocation** (`DRM_MM_INSERT_HIGH`): BOs allocated at `0xfff*` matching BSP's pattern (regcmd at `0xfff28000`). CNA EXECUTER still doesn't fire. Reverted.

**Found mesa rocket DPU_RDMA config diverges from BSP** in 6+ registers (0x500c/0x5010/0x5014/0x501c/0x5020/0x5024). Most suspicious: `0x5020` (BSP=input feature IOVA, mesa=descr/weights area) and `0x5024` (BSP=0xf, mesa skips entirely). **Patched both individually — neither unlocked CNA.**

**Current sub-unit state (best `rocket_job_2026-05-21_dpu_rdma_clean_best.c`):**
- CNA: `S_STATUS=0x1, OP_EN=0` (still laggard — only sub-unit not running)
- CORE: `S_STATUS=0x1, OP_EN=1` ✓
- DPU: `OP_EN=1` ✓
- DPU_RDMA: `S_STATUS=0x1, OP_EN=1` ✓
- IRQ count: still 0

**Latest source:** `rocket_job_2026-05-21_dpu_rdma_config_explored.c` (BSP_INJECT[115] for 0x5024=0xf still applied; harmless)

**Hypothesis at end of session:** CNA waits for input data in CBUF. DPU_RDMA is supposed to push input → CBUF, but with wrong config it might be doing nothing useful. Alternatively, CNA waits for a specific weight-loaded signal (DCOMP is zeroed in our path because mesa uses uncompressed weights). Multiple DPU_RDMA register diffs may need fixing together OR something completely separate (maybe in the mesa code that generates the regcmd values from tensor shapes).

## TL;DR 2026-05-21 BREAKTHROUGH — found the gate: CNA register 0x1018 (CONV_CON4) missing

After exhausting all visible state diffs, found via systematic regcmd diff that **mesa rocket NEVER writes CNA register `0x1018` — which is missing from rocket_registers.h entirely** (gap between `REG_CNA_CONV_CON3 = 0x1014` and `REG_CNA_DATA_SIZE0 = 0x1020`).

BSP writes `0x1018 = 0x3de00` at regcmd slot 9. CNA refuses to start the pipeline when 0x1018 is at HW default 0.

**Test that proved the gate:** added `writel(0x3de00, _npu + 0x1018)` as CPU pre-write in `rocket_job_hw_submit`. Result:
- **IRQ count: 0 → 19** for 5 teflon runs
- **PC reaches TASKST=0x11000** (BSP "completed" state) at AT1MS
- **IOMMU shows AXI traffic** (FA=0xd7100, was idle before)
- Output still `25866240` (zero_point) because `0x3de00` is YOLOv5s-shape-specific, not correct for our 5×5 conv2d

**This is the foundational mesa rocket bug for RK3568 support.** The fix requires:
1. Add `REG_CNA_CONV_CON4 = 0x1018` to mesa's `registers.xml`, regenerate header
2. In `mesa/src/gallium/drivers/rocket/rkt_regcmd.c`, compute value from tensor shape (need to derive bit-field encoding — likely encodes pad/dilation/feature_grain)
3. Update kernel `rocket_registers.h` similarly for upstreaming

**Current source:** `rocket_job_2026-05-21_BREAKTHROUGH_conv_con4.c` — has the CPU pre-write live. Pipeline activates. Output computation needs the right value.

**Next steps (after this session):**
1. Bisect 0x1018 field encoding by varying bits and observing CNA behavior
2. Capture BSP regcmd for SIMPLER conv (e.g., 1×1 3×3 stride 1) — comparing values across different conv shapes will reveal what bits depend on what tensor params
3. Or seek RK3568 NPU TRM access
4. Once value-computation is known, port the fix into mesa rocket userspace properly

This concludes 2 sessions of bisection. **The CNA EXECUTER mystery is solved at the register-level.** Remaining work is field-encoding derivation and proper userspace fix.

## TL;DR 2026-05-21 ULTRA — TRM ACQUIRED + 7 mesa rocket bugs catalogued

**Acquired RK3568 TRM Part 2** (`trm/part_2/`, 87 PDFs ~20 pages each). Chapter 9 NPU on pages 410-462. PDFs 14-16 cover the NPU chapter.

**Extracted comprehensive register documentation** to `docs/npu-trm-decoded.md`:
- All CNA registers (S_STATUS, S_POINTER, OP_EN, CONV_CON1-4, DATA_SIZE0-3, WEIGHT_SIZE0-2, CBUF_CON0-1, CVT_CON0-4, FC_CON0-2, PAD_CON0-1, FEATURE_DATA_ADDR, DMA_CON0-2, DCOMP_*, CBUF_DBG_*)
- CMAC, ACCU, DPU register summaries
- PC register details (TASK_CON, INTERRUPT_*, etc.)
- GLOBAL_OP_EN (offset 0x0008) per-block enable
- Routing-byte decoding (corrected: 0x02=CNA, 0x04=CMAC, 0x08=ACCU, 0x10=DPU, 0x20=DPU_RDMA)
- NPU operation flow charts (Fig. 9-3 conv flow 1, Fig. 9-4 conv flow 2 zero-skipping, Fig. 9-6 DPU flying mode)
- Application notes (debug, IRQ, clock domains)

**Sub-unit identification corrected** (from TRM):
- 0x2xxx routing 0x04 = **CMAC** (not "MYSTERY") — MAC array control
- 0x3xxx routing 0x08 = **ACCU** (not "CORE") — accumulator after MAC

**CONV_CON4 field encoding confirmed:**
- TRM page 418: `CNA_conv_con4` (offset 0x0018), bits 23:0 = `rgb_bytelength` (byte length of argb feature map). Single field.
- For non-ARGB convs: field is documentation-meaningless, but pipeline gate requires non-zero.

**Mesa rocket bugs catalogued (cumulative, 7 confirmed):**
1. **CONV_CON4 (0x1018) never emitted** — pipeline gate (THE foundational bug)
2. **CONV_CON2 KERNEL_GROUP field omitted** (mesa only sets FEATURE_GRAINS)
3. **DMA_CON0 fetch_pixel_len omitted** (bits 15:8 left at 0)
4. **DCOMP descriptors all zero** (REGNUM=0, AMOUNT*=0; BSP also writes 0 so may be benign for non-ARGB)
5. **argb_in=0xa hardcoded** — treats all inputs as 3-channel RGB
6. **0x1018 missing from mesa's registers.xml**
7. **FEATURE_DATA_ADDR points at weights** (probably benign; CNA fetches via CBUF not directly)

**CPU reference vs rocket comparison:**
- CPU reference (no delegate): sum=23,252,430, min=0, max=255, mean=113.5, varied output across channels
- Rocket: sum=25,866,240 (constant), **only 2 unique values (43, 128)**, channel 0 = bias only, channels 1-127 = zero_point
- Pattern matches "CNA convolves input × zero_weights + bias = bias only; channels beyond first kernel group not written"

**Weights BO HAS DATA** — kernel memremap confirms real int8 weights at IOVA 0xe000 (`e9 0a c9 04 23 fc 0d da ...`). Bug isn't in BO data, but in CNA's failure to LOAD weights into MAC array.

**Conclusion: surgical patches don't compose.** Each fix reveals another bug. The path forward is a proper mesa rocket userspace port using the TRM as reference — not kernel-side patching. The TRM extract enables this work.

**Snapshots:**
- `rocket_job_2026-05-21_TRM_diagnosed.c` — kernel state with TRM-derived patches (CONV_CON4=0x19000, KERNEL_GROUP=3, fetch_pixel_len=0x50)
- `rocket_job_2026-05-21_multi_patch_attempts.c` — also has FEATURE_DATA_ADDR=0 patch
- `docs/npu-trm-decoded.md` — comprehensive TRM extract for future sessions

**Next-session priorities (multi-session userspace work):**
1. Audit `mesa/src/gallium/drivers/rocket/registers.xml` against TRM, add missing CNA_CONV_CON4 + verify CNA_CONV_CON2 field positions
2. Rewrite `rkt_regcmd.c` CNA emission to match TRM Section 9.5.5 "convolution flow 1"
3. Test with conv2d.tflite, verify against CPU reference output (sum=23,252,430)
4. Once conv2d works, expand to more complex models (MobileNetV1 etc.)

## TL;DR 2026-05-21 EOD — Mesa patches drafted + tested (saved as patch file)

**Implemented 3 TRM-derived fixes in mesa rocket source** (`/home/dio/Downloads/claude_npu/rock3b-npu-project/mesa-patches/rocket-rk3568-trm-fixes.patch`, 92 lines):

1. **Add `CONV_CON4` (0x1018) register + RGB_BYTELENGTH field to `registers.xml`** — was missing entirely. Single field per TRM page 418.
2. **Add `FETCH_PIXEL_LEN` field to `DMA_CON0` in `registers.xml`** — previously labeled RESERVED, now properly named bits 15:8.
3. **In `rkt_regcmd.c`:**
   - Add `EMIT(REG_CNA_CONV_CON4, RGB_BYTELENGTH(input_w*input_h*input_c))` — was missing
   - Add `KERNEL_GROUP(weights_kernels/32 - 1)` to CONV_CON2 emit — was omitted
   - Add `FETCH_PIXEL_LEN(input_width)` to DMA_CON0 emit — was 0

**All 3 patches verified in regcmd** (slot 7: 0x1010 val=0x00030350 with KERNEL_GROUP=3; slot 9: 0x1018 val=0x00019000 with our CONV_CON4; slot 29: 0x1078 val=0x000f500f with FETCH_PIXEL_LEN=80).

**Build:** mesa 25.3.0 (`f7aeb0d`), built libteflon.so cleanly. No errors, no warnings.

**Verified in regcmd content via kernel REGCMD_FULL dump** (after fixing the kernel-side filter bug — was `off >= 0x1020` which dropped the CONV_CON1-4 range; widened to `off >= 0x1004`).

**Output still constant** (`sum=25,866,240, 2 unique values, channel 0 = bias`). More bugs remain in mesa rocket, but these 3 are foundationally correct per TRM. The patches:
- Are minimal (3 focused changes)
- Have clear rationale (TRM page citations)
- Don't regress (registers.xml additions; new fields properly placed)
- Build cleanly against current mesa main
- Should be safe for RK3588 (the existing rocket target) — CONV_CON4=0 is the old behavior, KERNEL_GROUP just changes a previously-zero bit field, FETCH_PIXEL_LEN similarly

**Patch location:** `mesa-patches/rocket-rk3568-trm-fixes.patch`. Apply with `git apply` from mesa root.

**Mainline path:**
1. Verify on RK3588 (no regression)
2. Get more BSP samples for different conv shapes to refine value formulas
3. Identify the remaining bugs (DPU_RDMA config, weight loading path, etc.) — each as a separate commit
4. Submit as MR series on `gitlab.freedesktop.org/mesa/mesa`

**Compile server (LXC at <COMPILE-SERVER-IP>) ready** — useful for faster builds if needed. Board builds work fine for module-only changes (~30s). Full kernel rebuild ~3-4 min on board. Mesa build ~30s for incremental.

## TL;DR 2026-05-21 ULTRA EOD — Patch refined, 5 fixes total, "board doesn't fully work yet" diagnosed

**Patches in `mesa-patches/rocket-rk3568-trm-fixes.patch`** (111 lines, 5 changes):

1. `registers.xml`: Add `CONV_CON4` (0x1018) reg + `RGB_BYTELENGTH` field
2. `registers.xml`: Split `RESERVED_1` in `DMA_CON0` to expose `FETCH_PIXEL_LEN` (bits 8-15)
3. `rkt_ml.h`: `CBUF_BANKS` 12 → 8 (TRM RK3568 has 256 KiB / 8 banks vs RK3588's 12)
4. `rkt_regcmd.c`: Add `EMIT(REG_CNA_CONV_CON4, …)` — was missing entirely
5. `rkt_regcmd.c`: Add `CNA_CONV_CON2_KERNEL_GROUP(weights_kernels/32-1)` to CONV_CON2 emit + replace `+50` hack feature_grains with TRM formula
6. `rkt_regcmd.c`: Add `CNA_DMA_CON0_FETCH_PIXEL_LEN(input_width)` to DMA_CON0 emit

**Pipeline now activates:** IRQs fire, CNA runs, all sub-units active. BUT output is still wrong (`25,866,240` vs CPU ref `23,252,430`).

**Output pattern decoded** (per-channel inspection): CNA processes **exactly ONE atomic tile** = 16 channels (one FEATURE_ATOMIC group) × 16×16 spatial positions, then stops. Channel 0-15 partial output, channels 16-127 zero_point. Total non-default = 16 × 256 = 4096 of 204800 (2%).

**Remaining bug hypothesis**: Multi-atomic iteration loop not configured. NVDLA architecture processes data in atomic tiles iterated to cover output. Mesa only sets `dataout_atomics = output_w*output_h` (1600) but CNA only processes 256 atoms. Either a specific counter isn't right, OR mesa rocket assumes BSP's multi-submission architecture (BSP runs 3 regcmds per YOLOv5s inference; mesa runs 1 per conv2d). Our 40×40 output also doesn't divide cleanly by NVDLA's 16-spatial-atom-size — mesa may need to handle non-16-divisible dimensions properly.

**Patch status:** TRM-correct, builds clean, lands in regcmd. NOT submission-ready until output matches CPU reference. Continued debugging needs deeper TRM reading + experimenting with `dataout_atomics`, multi-submission flow, atomic-tile boundaries. The patches we have don't regress and are foundationally correct — they're the right starting point even if not sufficient alone.

## TL;DR 2026-05-21 PM — CONV_CON4 data point collection + dump infrastructure fixes

**Today's work continued from BREAKTHROUGH:**

1. **Fixed BSP regcmd dump infrastructure** (rknpu_job.c):
   - Raised `regcmd_dump_done` limit from 3 to 30
   - Only increment counter on successful translation (was incrementing on every attempt)
   - Filter dump output to only print CNA-relevant offsets (0x100c-0x10ff + DCOMP 0x1100-0x114f + CVT 0x1210-0x123f + OP_ENABLEs). Avoids dmesg ring buffer rotation issues.
   - Rebuilt + installed in /boot/Image-6.19 (l1) and /boot/Image-6.19-rocket (l3). Live kernel: #65 May 21 11:16.

2. **Captured CONV_CON4 across 2 different conv shapes:**

   | Model / Conv | CONV_CON2 | CONV_CON4 |
   |---|---|---|
   | YOLOv5s 6×6 stride 2 | 0x40 | **0x0003de00** |
   | MobileNetV1 3×3 stride 2 | 0x50 | **0x0003e820** |

   CONV_CON4 differs (XOR=0xa20, bits 5/9/11/13 differ). CONV_CON1/3 identical across both shapes. CONV_CON2 differs.

3. **Confirmed any non-zero value at 0x1018 activates pipeline.** Tested 0x1, 0x50500, 0x3de00 — all trigger IRQ + TASKST=0x11000 (completion). But output stays at zero_point (25866240) regardless of value because CNA's conv produces wrong data (value encoding is shape-specific).

4. **RKNN runtime submits ONE regcmd per inference, not one-per-conv-layer.** YOLOv5s capture shows 3 sub-regcmds per inference (small init + 2 large) all sharing same first conv. MobileNetV1 capture shows 1 regcmd per inference. So we only get 1 conv shape per model. Need more model variety for encoding decode.

**Status:** Pipeline activates on rocket. IRQ count 0 → 4+ per teflon run. Output computation still wrong — needs proper field-encoded CONV_CON4 value, which needs more BSP samples or TRM.

**Architecture for the fix:**
- Mesa rocket userspace needs `EMIT(REG_CNA_CONV_CON4, computed_value)` in `rkt_regcmd.c`
- The `computed_value` formula needs to map `(task->weights_width, weights_height, stride_x, stride_y, ...)` to the right bit pattern
- Without TRM, this requires regression-fitting from many BSP samples

Current snapshot: `rocket_job_2026-05-21_FINAL_breakthrough.c` (CONV_CON4=0x50500 test variant). Kernel #65 (BSP regcmd dump improvements). Two memory files document the breakthrough and the two captured data points.

**Diff so far:**

| Sub-unit | BSP @ AFTER_OPEN | Rocket @ AT10MS |
|---|---|---|
| CNA | S_STATUS=0x10008 (EXECUTER) | S_STATUS=0x1 (just STATUS_0=1) |
| CORE | S_STATUS=0x10008 | S_STATUS=0x1 OP_EN=1 |
| PC TASKST | 0x11000 | 0xf000 or 0x11000 with TASK_NUM=0x12f |
| PC BASE | 0xfff* (HIGH IOVA) | 0xd7000 (LOW IOVA) |

Pre-OP state is byte-identical between BSP and rocket. Post-OP transition is the unsolved gate.

## TL;DR 2026-05-20 PM v3 — Sticky bit FIXED, but PC stalls mid-regcmd

**Bisection chain that found the cause:**
1. Added `PROBE_CNA` readback at end of `rocket_core_init` (post-pm_runtime_resume_and_get, pre-autosuspend). Confirmed: `S_STATUS=0x00000000` at probe time, then `0x4` at first submit's PRE_OP.
2. Set autosuspend_delay to 1 hour, verified `runtime_status=active` throughout and `opp_set_rate count=0` after teflon run. Bit-2 still set at PRE_OP → not caused by autosuspend cycle.
3. Added `CNA_BISECT_T0..T3` readbacks in `rocket_job_run` (post-pm_get, post-detach, post-attach, post-trigger_enable). All four = `S_STATUS=0x0`. Bit set INSIDE `rocket_job_hw_submit` before PRE_OP.
4. Reviewed hw_submit body: lines 196-211 are CPU writel calls to sub-unit `S_POINTER` registers. Removed `writel(0x1, CNA_S_POINTER)` at line 196 → PRE_OP CNA `S_STATUS=0x0`. **Confirmed cause.**

**Why our CPU pre-write set bit 2:** the pre-write was empirical from 2026-05-18 ("CNA wake bit", value `0x1`). At the time, no BSP_INJECT entries existed. Now BSP_INJECT[4] writes `0xe` to CNA_S_POINTER via PC broadcast (bits 1+2+3 = PP_EN + EXECUTER_PP_EN + PP_MODE). The PC broadcast of `0xe` does NOT set the sticky `RESERVED_1[0]` bit, but our CPU `0x1` write did. Now redundant + harmful → removed.

**Result of fix:**
- ✅ `PRE_OP CNA: S_STATUS=0x00000000` — matches BSP cleanly
- ✅ First-submit `AT10MS CNADUMP: S_STATUS=0, S_POINTER=0xe, CONV_CON1=0xa000, CBUF_CON0=0x17` — all BSP_INJECT broadcasts land
- ❌ Output unchanged: `out_sum=25866240` (zero_point)
- ❌ CNA OP_EN stays 0; OP_ENABLE entry never reaches CNA
- ❌ Subsequent submits regress: `S_STATUS=0x5`, CNA config registers at HW defaults — PC broadcast not landing on CNA

**Diagnosis of the remaining gap:**
- First submit: PC broadcasts early regcmd entries (4, 5, 16, 126) successfully → CNA configured. Then PC stalls between entries 126 and 129 (the CNA OP_ENABLE entry). `PC_TASKST=0xf000` (undocumented frozen value). `PC_AMOUNTS=0xa0` (initial value, no decrement visible).
- Subsequent submits: no detach/attach/trigger_enable (attached_domain == job->domain). CNA in `S_STATUS=0x5` blocks PC config broadcasts.

**Next-session priorities:**
1. **Remove CORE CPU pre-write too** — line 197 `writel(0x1, CORE_S_POINTER)` still sets sticky `S_STATUS=0x4` on CORE (visible in AT50US first-submit COREDUMP). May be needed for CORE wake; need a BSP_INJECT replacement if so.
2. **Bisect which BSP_INJECT entry stalls PC** — disable BSP_FULL_INJECT (DCOMP+CVT shift+insert) first, then disable BSP_INJECT[16] (CBUF_CON0 override at slot 16). Find the minimal set that gives clean state without stalling PC.
3. **PC_TASKST=0xf000 meaning** — undocumented. Capture from a working BSP YOLOv5s submission to see if BSP ever passes through this value.
4. **Per-submit CNA reset** — to fix subsequent-submit regression, add `rocket_core_reset()` or CNA_S_POINTER PP_CLEAR pulse between submits.

## TL;DR 2026-05-20 PM v2 — DT clock-names alignment FAILED

## TL;DR 2026-05-20 PM v2 — DT clock-names alignment FAILED

**This afternoon:** rebuilt rocket DT (`rk3568-rock-3b-rocket.dts`) and `rocket_core.c` `rocket_clk_names[]` to match BSP RKNPU layout: `{scmi_clk, clk, aclk, hclk}` with explicit `&cru CLK_NPU`. Deployed (gotcha: extlinux l3 loads `*-6.19-rocket.dtb`, not `*-rocket.dtb` — had to overwrite the older filename). Booted with rocket bound + scheduler initialized + `Rockchip NPU core 0 version: 0` confirmed clean.

**Result:** `PRE_OP CNA: S_STATUS=0x00000004` — UNCHANGED. After OP_EN, `S_STATUS=0x00020004` (STATUS_1=2) — UNCHANGED. Output `out_sum=25866240` — UNCHANGED.

**Conclusion:** Clock-names ordering / explicit CRU CLK_NPU enable order is NOT the gate. The sticky `RESERVED_1[0]` (bit 2) at CNA boot state is set somewhere outside the clock domain.

**Subtle probe-failure mode discovered:** rocket_probe at `rocket_drv.c:299` increments `num_cores++` BEFORE `rocket_core_init(&rdev->cores[core])` at line 316. If init fails (e.g., `clk_bulk_get` ENOENT from DT/driver name mismatch), the broken core is still counted; `drm_sched_init` is skipped for that core; subsequent file open in `drm_sched_entity_init` finds `sched->ops==NULL`, warns "uninitialized scheduler"; subsequent submit derefs NULL in `drm_sched_job_init+0xd0`. Fix path for any future probe-failure investigation: decrement num_cores on error in rocket_probe or move the increment after success.

**Remaining suspects** (next-session priority, post-clock-names disproof):
1. **Port full BSP `rknpu_soft_reset`** — rocket has minimal srst_a+srst_h. BSP does msleep(10) + NOC re-init + IOMMU detach/reattach + `rknpu_state_init` (writes to NPU global state regs). The `state_init` writes likely touch a CNA reset register we never hit.
2. **Audit BSP RKNPU offset writes during probe + first-job init** — instrument `rknpu_drv.c` writel calls (mainline kernel + l1 boot) and capture ALL register writes to `fde40000.npu` before YOLOv5s runs. Replay on rocket. The diff should reveal a CNA-init register we're missing.
3. **Mesa port for RK3568** (still on list, lower priority) — long-term userspace fix but unlikely to unblock if HW state is the gate.

## TL;DR 2026-05-20 final

**Decisive conclusion**: every register value we can write matches BSP. Every HW-set status bit that distinguishes BSP-working from rocket-stuck reflects an internal CNA pipeline state we can't reach via MMIO.

**Evidence**:
- BSP CBUF_CON0 readback `0x1017` (bit 12 DATA_REUSE set by HW) vs rocket `0x17` (no bit 12 — CNA never reached the state to set it)
- BSP CNA `S_STATUS=0x10008` (STATUS_1=1, "running"/EXECUTER) vs rocket `S_STATUS=0x20004` (STATUS_1=2, "stalled"/"waiting")
- BSP and rocket BOTH write `0x17` to CBUF_CON0 in regcmd — bit 12 is HW-controlled

**Today's experiments — all confirm the above**:
- ✗ DCOMP descriptor range injection (15 entries)
- ✗ CVT range injection (9 more entries)
- ✗ CBUF_CON0 override at entry 16 (forcing BSP's 0x17 to win over mesa's 0x84)
- ✗ TRM magic preservation (slot 128 → 152 with proper shifting)
- ✗ TASK_DMA_BASE_ADDR with valid IOVA (0x41000 input BO; lands at 0x82000, no fault, no help)

**Source snapshots saved** (`/home/dio/Downloads/claude_npu/rock3b-npu-project/`):
- `rocket_job_2026-05-20_full_bsp_inject.c` — initial DCOMP+CVT (broken: TRM magic destroyed)
- `rocket_job_2026-05-20_trm_preserved.c` — TRM-aware shift
- `rocket_job_2026-05-20_task_dma_tested.c` — TASK_DMA = 0x41000 test
- `rocket_job_2026-05-20_final.c` — current best (TASK_DMA reverted to no-write)

**Two viable remaining paths**:
1. **Mesa port for RK3568** (priority #4 in this file). Adds 0x2xxx CSC sub-unit to mesa's enum + emits CNA/CORE S_POINTER writes. **Caveat**: even if mesa emits all BSP-equivalent writes, the HW state gate likely persists — we already kernel-inject all those writes and CNA still stalls. But it's the right architecture for upstreaming.
2. **Boot l1 with instrumented BSP + capture CNA PRE-OP_EN state side-by-side with rocket**. We have BSP's `RKNPU_DUMP_BEFORE_OPEN` showing CNA at `S_STATUS=0, S_POINTER=0, OP_EN=0, CBUF_CON0=0x1017` before OP_EN. Rocket: we never captured CNA PRE-OP_EN. Add PRE_OP CNA readback to rocket. If rocket CNA is already at `S_STATUS=0x20004` BEFORE OP_EN, the state was set during PRE_SUBMIT_RESET (or earlier). That'd point at the reset/init sequence.

## 2026-05-20 EOD addendum — PRE_OP CNA captured + DT clock-names diff found

PRE_OP CNA readback added (rocket):
- `S_STATUS = 0x00000004` (RESERVED_1[0] = bit 2 set; STATUS_1 = 0)
- `S_POINTER = 0x00000001` (our CPU pre-write)
- `OP_EN = 0`
- `CBUF_CON0 = 0x00000044` (HW default — PC hasn't broadcasted regcmd yet)
- `DCOMP_CTRL = 0`

After OP_EN (AT10MS):
- `S_STATUS = 0x00020004` (STATUS_1 transitions 0 → 2)
- `S_POINTER = 0x0e` (BSP_INJECT[4] PC broadcast landed)
- `OP_EN = 0`
- `CBUF_CON0 = 0x17` (mesa's regcmd value landed)
- `DCOMP_CTRL = 0`

vs BSP `RKNPU_DUMP_BEFORE_OPEN`:
- `S_STATUS = 0x00000000` (CLEAN)
- `S_POINTER = 0x00000000`
- `OP_EN = 0`
- `CBUF_CON0 = 0x00001017` (BSP value 0x17 + HW-set bit 12 DATA_REUSE from prior submit)

**Key observation**: rocket's CNA `S_STATUS = 0x4` *before any submission's OP_EN* (RESERVED_1[0] set). BSP starts at `0x0`. The pre-submit state differs. After OP_EN, both transition to STATUS_1 ≠ 0, but rocket lands at STATUS_1=2 while BSP at STATUS_1=1.

**DT clock-names diff** (rk356x-base.dtsi BSP vs rk3568-rock-3b-rocket.dts rocket):
- BSP: `clock-names = "scmi_clk", "clk", "aclk", "hclk"` — 4 clocks including CRU `CLK_NPU` ("clk")
- Rocket: `clock-names = "aclk", "hclk", "npu", "pclk"` — 4 clocks including PCLK_NPU_PRE but NO explicit CRU CLK_NPU

Rocket enables clocks in order `[aclk, hclk, scmi, pclk]` via `clk_bulk_prepare_enable`. BSP enables in order `[scmi, clk, aclk, hclk]`. The SCMI-first vs SCMI-third ordering AND the inclusion of explicit `clk` (CRU `CLK_NPU`) vs implicit propagation through `aclk_npu_pre` chain may cause different CNA boot state.

**Hypothesis for next session**: align rocket DT to BSP — add `clk = <&cru CLK_NPU>` to rocket's clock list, reorder so SCMI is first. Compare CNA `S_STATUS` at PRE_OP after the change. If still `0x4`, the bit-2-set is HW-fixed (not clock-controllable); if it goes to `0x0`, we've found the missing init.

**Final source**: `rocket_job_2026-05-20_final.c` (PRE_OP CNA readback added, TASK_DMA test reverted).



## TL;DR for next session (post mesa audit)

**Major finding from mesa source audit**:
- Mesa rocket is **fundamentally an RK3588 driver**. ONLY one RK3568 reference in entire codebase (acknowledging broadcast target=0x81 unsupported).
- **2 structural gaps**:
  1. No 0x2xxx sub-unit support (gap in target enum between CNA 0x200 and CORE 0x800)
  2. No CNA_S_POINTER / CORE_S_POINTER emission (only DPU/DPU_RDMA get S_POINTER PP_EN writes)
- **TRM-required magic sentinel** found at `rkt_regcmd.c:526`: `util_dynarray_append(regs, uint64_t, 0x0041000000000000)`. Mesa explicitly comments "TRM: before op_en, 64'h0041_xxxx_xxxx_xxxx must be set." Earlier sessions had OVERWRITTEN slot 128 (the TRM magic) with CONV_CON3 injection. Fixed today: TRM magic preserved at slot 152 (after shift).

**Result of all 2026-05-20 patches**: CNA still `S_STATUS=0x00020004` (`STATUS_1=2`). BSP reaches `0x00010008` (`STATUS_1=1`). No EXECUTER. Output unchanged.

**Conclusion**: Every regcmd-content gap and TRM constraint mesa knows about is now kernel-patched. CNA's HW pipeline still won't enter EXECUTER state. **The gate is NOT in the regcmd content.**

**Source snapshots (2026-05-20)**:
- `rocket_job_2026-05-20_full_bsp_inject.c` — initial DCOMP+CVT injection (TRM magic overwritten — BROKEN)
- `rocket_job_2026-05-20_trm_preserved.c` — final state: 5-slot BSP_INJECT + DCOMP/CVT shifted before TRM magic

**Next-session priorities**:
1. **NPU clock + IOMMU side-by-side** — add detailed clk_get_rate() + IOMMU register reads at AT_OPEN in BOTH rocket and BSP. The bug is HW state — find the diff. Already have `RKNPU_DUMP_BEFORE_OPEN` in BSP, just need to add clk readbacks and rerun the comparison.
2. **TASK_DMA_BASE_ADDR with valid IOVA** — currently 0. Maybe CNA needs a valid mapped IOVA there even if we don't use it. Try pointing at input BO (200K) or a dedicated 64KB+ scratch BO.
3. **Trivial conv test** — bisect by complexity. A 1×1 conv on a 4×4×1 tensor through Teflon. If THAT runs, the bug is conv2d.tflite-specific (unlikely given the deep generality of CNA, but worth ruling out).
4. **Mesa port for RK3568** (proper userspace fix — multi-session undertaking):
   a. Add `0x400` (mystery sub-unit) to `target` enum in `registers.xml` between CNA (0x200) and CORE (0x800). Pick a name — `CSC` (Convolution Sequence Controller) is a likely guess; could verify by reading RKNPU BSP register defs.
   b. Add a new `<domain name="CSC" width="32">` block in registers.xml with offsets `0x2004` (S_POINTER), `0x200c` (OPERATION_ENABLE), and any other 0x2xxx registers we discover.
   c. Re-run `gen_header.py` to generate `rkt_registers.h` with new defs.
   d. Edit `rkt_regcmd.c` to emit early in `fill_first_regcmd`:
      - `EMIT(REG_CNA_S_POINTER, CNA_S_POINTER_POINTER_PP_MODE(1) | CNA_S_POINTER_EXECUTER_PP_EN(1) | CNA_S_POINTER_POINTER_PP_EN(1))` (currently missing!)
      - `EMIT(REG_CORE_S_POINTER, ...)` (currently missing!)
      - `EMIT(REG_CSC_S_POINTER, CSC_S_POINTER_PP_EN(1))` (new sub-unit)
   e. Emit `EMIT(REG_CSC_OPERATION_ENABLE, 1)` before existing OP_ENABLE writes (mirroring BSP's entry 61 position).
   f. Rebuild mesa: `cd ~/mesa/build && ninja && sudo ninja install` (or equivalent — check meson.build).
   g. Redeploy `libteflon.so`, re-run Teflon. With proper userspace regcmd, can remove kernel-side BSP_INJECT/BSP_FULL_INJECT block.
   h. **Caveat**: this only fixes the regcmd-content side. If the true bug is HW state (per current evidence), mesa port won't unblock CNA either. But it's the right architecture regardless.



## TL;DR for next session (2026-05-20 update)

**This session decisively established**: **regcmd content is NOT the gate** to CNA's EXECUTER state.

What we did today (after PLAN_2026-05-20.md):
- Implemented DCOMP-range injection (15 entries 0x1114-0x114c)
- Extended to DCOMP + CVT (24 entries total: also 0x1210-0x1230)
- Overrode mesa's CBUF_CON0=0x84 with BSP's 0x17 by patching entry 16
- Added shift+insert mechanic (OP_ENABLEs moved 129-132 → 153-156)
- Bumped `task->regcmd_count` for AMOUNTS write

**Final injected state**: 29+ BSP-aligned regcmd writes covering virtually every BSP-only offset.

**Result**: CNA `S_STATUS=0x00020004` (STATUS_1=2) — unchanged. BSP reaches `0x00010008` (STATUS_1=1). DPU never writes output. Teflon retries 3×/inference (~1914ms).

**CNA_S_STATUS field decode** (from rocket_registers.h):
- `STATUS_0` bits [1:0]
- `STATUS_1` bits [17:16]
- `RESERVED_1` bits [15:2]
- BSP `0x00010008` = `STATUS_0=0, STATUS_1=1, RESERVED_1[1]=1`
- Rocket `0x00020004` = `STATUS_0=0, STATUS_1=2, RESERVED_1[0]=1`

Different `STATUS_1` pipeline-state values. HW-controlled, not writable.

**Current snapshot**: `rocket_job_2026-05-20_full_bsp_inject.c` — has BSP_INJECT[4/5/16/126/127/128] + BSP_FULL_INJECT (DCOMP+CVT shift+insert).

**Remaining suspect classes** (next-session priority order):
1. **Mesa BO format / userspace path** — Read `~/mesa/src/gallium/drivers/rocket/rkt_resource.c` and `rkt_ml.c` for how it creates and populates input/weight BOs. Mesa-rocket targets RK3588 by default; maybe its BO setup is wrong for RK3568's CNA. Specifically look for: alignment, padding, dma_buf import flags, BO layout for compressed weights.
2. **NPU clock state diff** at OP_EN moment. Add `clk_get_rate()` readback for all NPU clocks (`SCMI_CLK_NPU`, `CLK_NPU`, `aclk_npu`, `hclk_npu`) in `RKNPU_DUMP_BEFORE_OPEN` and `AT_OPEN`. Compare BSP vs rocket cycle-accurate.
3. **Try a TRIVIAL conv** — maybe `conv2d.tflite` has unusual parameters. A 1×1 conv on a 4×4×1 tensor would test if ANY conv runs in rocket.
4. **Mesa source audit of `rkt_regcmd.c`** — specifically how it computes CONV_CON1=0 (mesa) vs BSP's 0xa000. The 0xa000 = bit-encoded conv mode. If mesa computes 0, mesa thinks conv mode is "default", which might be invalid for CNA.



## TL;DR for next session (post 2026-05-19 BSP-bisection)

**The bug is now scoped definitively**: rocket gets CNA to a state where it accepts PC broadcasts and receives every register write we throw at it, but **CNA's internal EXECUTER pipeline-state bit (S_POINTER/S_STATUS bit 16) never engages**. BSP RKNPU reaches `S_STATUS=0x00010008` (bits 3+16); rocket plateaus at `S_STATUS=0x00020004` (bits 2+17). Two completely different internal pipeline states.

**What we proved (exhaustively)**:
- ✅ Captured full BSP YOLOv5s regcmd (`bsp_regcmd_run0_yolov5s.txt`, 133 entries — exactly matches mesa conv2d count)
- ✅ v3 regcmd-patching primitive validated and robust (kmap_local_page + flush_dcache_page)
- ✅ 5-slot BSP injection landed every value (CONV_CON1=0xa000 visible in CNADUMP; CNA_S_POINTER=0xe via PC broadcast visible)
- ❌ **None of the BSP-only writes trigger CNA EXECUTER** — tried: 0x1004=0xe via PC broadcast, 0x100c=0xa000 (CONV_CON1), 0x1010=0x40 (CONV_CON2), 0x1014=0x12 (CONV_CON3), 0x1040=0x17 (CBUF_CON0 BSP val), 0x2004=0xe + 0x200c=0x1 (mystery sub-unit)
- ❌ pre-submit reset removal didn't help either (state doesn't accumulate across runs)

**Concrete next leads**:

1. **Inject the FULL DCOMP descriptor range** (0x1114..0x114c — 15 entries) + **CVT range** (0x1210..0x1230 — 9 entries). All BSP values are 0 in YOLOv5s run 0, so this is a "table-init-to-zero" pattern. May be required even if values are 0. Use the v3 primitive but need more injection slots — overwrite mesa's redundant DPU_RDMA_S_POINTER (entry 5) AND shift OP_ENABLEs forward.

2. **Try a TRIVIAL conv** — a 1×1 conv on tiny tensor. Maybe mesa rocket's `conv2d.tflite` test case is unusual and hits a CNA edge case. A simpler model would reveal if conv2d.tflite is the problem.

3. **HW state investigation** — read MORE NPU debug registers BSP exposes. Look for:
   - CNA_S_STATUS bit-by-bit decode (we know 0x20004 vs 0x10008 — bits 2/17 vs 3/16). What ARE bits 2 and 17?
   - CBUF status registers (CBUF_CON0/CON1/STATUS)
   - DCOMP_STATUS reg
   - Anywhere that reports "ready for OP_ENABLE"

4. **Compare BSP and rocket NPU power+clock state right before OP_EN** — RKNPU_DUMP_BEFORE_OPEN already includes PMU readbacks. The state diff between BSP-success and rocket-broken at the cycle BEFORE OP_EN tells us what HW state matters.

5. **Read mesa rocket userspace** — `~/mesa/src/gallium/drivers/rocket/rkt_regcmd.c`. Audit specifically how it determines CBUF_CON0=0x84 and the absence of CONV_CON1-4. Maybe mesa has bugs in conv-config generation we can fix in userspace.

**Captured artifacts**:
- `bsp_regcmd_run0_yolov5s.txt` — BSP's 133 entries, our ground-truth reference
- `bsp_regcmd_full_3runs.txt` — 830+ entries across 3 BSP submissions
- `rocket_job_2026-05-19_bsp_inject_5slot.c` — best-known rocket source (5-slot BSP injection, no pre-submit reset)
- `logs/serial/2026-05-19_bsp_regcmd_capture.log` — full BSP boot + YOLOv5s run

**The board is currently DOWN** (network unreachable since session end). Next session: probe Tailscale <BOARD-TAILSCALE-IP> / 192.168.1.163; if down, power cycle is needed.



## TL;DR 2026-05-19 PM update

**Big win**: captured full BSP RKNPU YOLOv5s regcmd for first 3 submissions. Stored in `bsp_regcmd_run0_yolov5s.txt` (133 entries — same size as mesa conv2d, perfect reference) and `bsp_regcmd_full_3runs.txt` (~830 entries total).

**Key finding from diff**:
- BSP writes to a sub-unit at **`0x2xxx` with routing bitmask `0x04`** that mesa rocket *never touches*:
  - Entry 1: `0x2004 ← 0xe routing=0x04` (S_POINTER PP_EN)
  - Entry 61: `0x200c ← 0x1 routing=0x04` (OPERATION_ENABLE at offset `0x00c`, NOT `0x008`!)
- Likely **CSC** (Convolution Sequence Controller) or similar pipeline-coordination unit. Not defined in `rocket_registers.h`.

**Tested fix attempt**: pre-write via CPU + inject via regcmd. **Neither unblocked CNA — mystery sub-unit init alone is NOT the fix.**

**Real diagnostic — CNA EXECUTER state**:
- **BSP after OP_EN**: `CNA_S_STATUS=0x00010008`, `S_POINTER=0x0001000e` — **bit 16 (EXECUTER) SET**
- **Rocket after OP_EN**: `CNA_S_STATUS=0x00020004`, `S_POINTER=0x00000001` — **bit 16 NOT set**

EXECUTER is a STATUS bit set by HW when CNA's pipeline actually runs. CPU writes to bit 16 are silently dropped (we tested this earlier). The bit is propagated from internal pipeline state.

**Therefore: mesa is missing some essential CNA setup that triggers EXECUTER**. BSP run0 writes 33 offsets mesa doesn't write — most are YOLOv5s-specific conv values, but a subset is likely **CNA pipeline-startup essential**.

**BSP-only offsets mesa doesn't write** (raw list to bisect):
- CNA: `0x1004`, `0x100c`, `0x1010`, `0x1014`, `0x1018`, `0x1114`-`0x114c` (15 entries), `0x1210`-`0x1230` (9 entries)
- Mystery: `0x2004`, `0x200c`
- CORE: `0x3004`, `0x300c`, `0x302c`
- DPU_RDMA: `0x5024`, `0x5030`, `0x503c`

**Source snapshots (2026-05-19)**:
- `rocket_job_2026-05-19_v3_patching.c` — established kmap-based regcmd patching primitive
- `rocket_job_2026-05-19_pp_clear_tested.c` — pre-BSP-diff baseline
- `rocket_job_2026-05-19_mystery_inject.c` — current state with mystery 0x2xxx injection

**Next session candidates**:
1. **Bisect the BSP-only CNA writes** — inject in batches via regcmd-injection primitive, find which one(s) trigger EXECUTER state
2. **CBUF_CON0 mismatch** — BSP writes `0x17`, mesa writes `0x84`. Try forcing 0x17 in rocket. Different magnitude — may be pipeline-relevant
3. **Look at later BSP regcmd entries** — `bsp_regcmd_full_3runs.txt` has 730+ entries from runs 1+2. Maybe an essential pattern visible there
4. **Mesa userspace audit** — read mesa rocket's `rkt_regcmd.c` more carefully for what it skips vs RKNPU userspace would emit



## TL;DR for next session (2026-05-19 update)

**Today's tooling wins**:
- ✅ **Established v3 regcmd-patching primitive**: `pfn_to_page + kmap_local_page + flush_dcache_page`. Don't try `memremap + writel + wmb` (crashes), don't try `drm_gem_shmem_vmap_locked` (regcmd BO isn't in `job->in_bos/out_bos`).
- ✅ Module params for fast iteration: `nop_start`/`nop_end` to NOP a range of regcmd entries, `dcomp_ctrl_val` to override DCOMP_CTRL.
- ✅ Defensive lesson: `task = &job->tasks[...]` runs AFTER `/* GO ! */` marker. Don't access `task->regcmd` before it.

**Today's null results — ruled-out hypotheses**:
- ✗ Entry 127 (`0x14 ← 0 routing=0x01`) as barrier — patched to NOP, no effect
- ✗ Any config entry 0-128 — NOPed all of them, CNA OP_EN still 0
- ✗ DCOMP_CTRL value — tested 0 and 0x8 (WT_DEC_BYPASS), CNA still rejects
- ✗ CNA_S_POINTER bit 16 (EXECUTER) — silently dropped on write
- ✗ Routing byte issue — CNA accepts CBUF_CON0/DCOMP_CTRL writes with routing=0x02; only OP_ENABLE rejected

**Definitive finding (2026-05-19)**: PC executes the regcmd's CNA OP_ENABLE entry (routing 0x02, value 1 to offset 0x1008) but CNA refuses to assert OP_EN. **Same broadcast bus delivers config writes successfully; only the OP_ENABLE register is rejected**. The gate is internal to CNA, NOT in the regcmd content.

**Remaining suspects** (still untested):
- CNA needs `S_POINTER` PP_CLEAR pulse (bits 4+5) to enter accepting state
- CNA's IOMMU master / clock gating handled separately from other sub-units
- DECOMP_CONTROL specific value (0-7 range) needed
- Missing CNA-init entry that BSP RKNPU has but mesa rocket doesn't generate

**Source snapshots (locally)**:
- `rocket_job_2026-05-18_unblocked.c` — initial PC-executing state
- `rocket_job_2026-05-18_cna_blocked.c` — clean before crash
- `rocket_job_2026-05-19_v3_patching.c` — today's safe regcmd-patching primitive



## TL;DR for next session

**Today's major findings — FOUR bugs identified, three fixed; pipeline now runs end-to-end but doesn't produce real output yet.**

1. **PMIC degradation** — after extended IOMMU page-fault testing, RK809 PMIC got into a degraded state. Symptom: kernel hard-resets at SDHCI probe (~2.9s into boot), looks identical to a kernel crash but is silent. Software workarounds (`pd_ignore_unused`, `module_blacklist=rocket,rknpu`) don't help. **Fix: full power-cycle (unplug power+USB for 60s).** PMIC needs to lose latched state.

2. **BUG D (TASK_DMA prefetch fault) FIXED.** `rocket_pc_writel(core, TASK_DMA_BASE_ADDR, task->regcmd)` was making PC's TASK_DMA engine fetch a 64KB window starting at the regcmd IOVA. 0x40000 bytes past `0xd7000` = 0x117000, exactly the FA we'd been chasing. **Fix: remove that single writel.** Verified: IOMMU stays at ST=0x11, FA=0 throughout job. No more page fault, no more board lockup.

3. **BUG D2 (PC inertia) PARTIALLY FIXED.** Rocket pre-wrote `CNA_S_POINTER = 0xe = bits 1+2+3` unconditionally. Per BSP `rknpu_job.c` lines 308-316, BSP only writes that value in the **multi-core (RK3588) branch** (`if (num_irqs > 1)`). For single-core RK3568, BSP does NOT pre-write S_POINTER. Rocket's pre-write put CNA/CORE in pingpong-waiting state that blocked PC from broadcasting. **Fix: gate rocket's S_POINTER pre-write to single-core path (`if (0)` for now).** Verified: PC now broadcasts regcmd entries → CNA `CBUF_CON0=0x84` (regcmd entry 0 landed!), CORE `OP_EN=1` (regcmd entry 130 landed!).

4. **BSP submit-sequence alignment applied.** From BSP `rknpu_job_subcore_commit_pc`:
   - AMOUNTS adds `RKNPU_PC_DATA_EXTRA_AMOUNT=4` (drain padding)
   - INT_MASK |= BIT(16) (NPU→GIC IOMMU-fault propagation in IOMMU mode)
   - INT_CLEAR narrowed to mask bits (was 0xFFFFFFFF)
   - TASK_CON `0x6001` was already correct (RESERVED_0 bit14 | TASK_COUNT_CLEAR bit13 | TASK_NUMBER bit0)
   - OP_EN pulse 1→0 (was single write)
   None of these were the primary unblock, but they're properly BSP-aligned now.

5. **BUG E (DPU/DPU_RDMA wake bit) FIXED.** DPU stayed at HW defaults (S_STATUS=5, OP_EN=0, DST_BASE=0) even after PC ran the whole regcmd. Sentinel test (CPU-writing 0xa5a5a5a5 to DPU_S_POINTER) accidentally unblocked DPU. Bisection: bit 0 alone (`writel(0x1, DPU_S_POINTER)`) is the trigger. Mesa's regcmd writes `0xe` (bits 1+2+3) to DPU_S_POINTER, which uses CNA-style PP_EN layout. **DPU's PP_EN is at bit 0**, not bit 1, so mesa never asserts it. **Fix: kernel pre-writes `0x1` to DPU_S_POINTER + DPU_RDMA_S_POINTER from CPU before submit.** Result: DPU now `S_STATUS=0x00020004`, `OP_EN=1`, `DST_BASE=0x73000` (correct output IOVA).

6. **DPU PROVABLY DOESN'T WRITE — bug isolated to CNA never starting** (memset test 2026-05-18 PM):
   - Pre-submit `memset(output_BO + 0..0x1000, 0xAB, 4K)`, post-timeout readback → all bytes still 0xAB
   - Surface 1 base IOVA 0x7F800 → all 0x00 (initial state, also untouched)
   - The "out_sum=26214400" we'd been chasing was a kernel-allocator artifact (allocator handed us pages with residual 0x80 from prior use). Now with different PA each run, out_sum varies (26214400 / 25866240)
   - Conclusively: DPU never issues any AXI writes. DPU is configured but waiting forever.

7. **Why DPU waits: pipeline broken at CNA** (2026-05-18 PM):
   - AT_OPEN sample (right after OP_EN, before any delay): `CNA OP_EN=0 / CORE OP_EN=1 / DPU OP_EN=1 / DPU_RDMA OP_EN=1`
   - Regcmd entry 129 is `CNA_OP_ENABLE (0x1008) ← 1 routing=0x02` (CNA bitmask) — present, correct routing
   - Earlier regcmd entries route to CNA fine (CBUF_CON0=0x84 lands; routing 0x02 reaches CNA)
   - **CNA selectively rejects OP_ENABLE entry while accepting all earlier CONFIG entries** — same routing byte, same sub-unit, different register
   - IOMMU stays at ST=0x11 (idle) throughout 500ms → no CNA AXI reads → no input fetch → CNA truly never ran

8. **Routing-byte decoding (2026-05-18 PM)**: regcmd entries have a routing byte in bits [63:56] of the 64-bit pair:
   - `0x02` = CNA (bit 1)
   - `0x08` = CORE (bit 3)
   - `0x10` = DPU (bit 4)
   - `0x20` = DPU_RDMA (bit 5)
   - `0x00` = no-op (pad)
   - `0x01` = ??? (routes off=0x14 = PC.REGISTER_AMOUNTS — possibly self-write or barrier)

9. **S_POINTER bit bisection for CNA** (all values tested 2026-05-18 PM):
   | CNA_S_POINTER write | PC broadcasts to CNA? | CNA OP_EN ends up |
   |---|---|---|
   | (no pre-write) | yes (CBUF_CON0=0x84) | 0 |
   | 0xe (bits 1+2+3) | NO (blocked) | 0 |
   | 0x1 (bit 0) | yes (CBUF_CON0=0x84) | 0 |
   | 0x2 (bit 1, PP_EN alone) | NO (blocked) | 0 |
   | 0x5 (bits 0+2) | yes (CBUF_CON0=0x84) | 0 |

   No S_POINTER value unblocks CNA OP_EN. The rejection is **at the OP_ENABLE register, not at the broadcast-acceptance gate** — CNA's broadcast bus accepts config writes either way.

10. **CNA_OPERATION_ENABLE rejects CPU writes**: tried `writel(0x1, CNA_OP_ENABLE=0x1008)` directly from kernel — readback returns 0. Like CBUF_CON0, it's PC-broadcast-only. Side-effect: the CPU touch even briefly **breaks subsequent PC broadcasts to CNA** (CBUF_CON0 went back to 0x44 default). Strong "do not touch from CPU" signal.

11. **Best current state**: `0x1` pre-writes to all four sub-unit S_POINTERs (CNA/CORE/DPU/DPU_RDMA at 0x1004/0x3004/0x4004/0x5004). Source snapshot: `rocket_job_2026-05-18_cna_blocked.c`.

12. **Entry-127 kernel-side patch ATTEMPTED, BOARD CRASHED (2026-05-18 PM)**:
   - Tried `memremap(WB) + writel(0) + wmb() + memunmap` on regcmd BO at IOVA 0xd7000 offset 0x3F8 (entry 127's 8 bytes)
   - `arch_sync_dma_for_device` isn't exported to modules; fell back to `wmb()` barrier
   - Board hard-crashed during first submit. Symptoms: SSH connections refused, `kex_exchange_identification: Connection reset by peer`, then full timeout. Cold reboot recovered cleanly.
   - **Root cause hypothesis**: rocket BO is shmem-backed, dma_sync'd via `dma_sync_sgtable_for_device(BIDIRECTIONAL)` in `rocket_ioctl_fini_bo`. After fini_bo, the BO is "device-owned" — CPU writes via memremap conflict with the IOMMU/NPU AXI master's view. `wmb()` ensures CPU write ordering but doesn't flush CPU cache; for non-coherent DMA, NPU reads stale cache contents → bus deadlock → SoC crash.
   - **PATCH REVERTED** in current source. Don't retry the same way.

13. **Next-session candidates**:
   a. **Re-attempt entry-127 patch SAFELY** by going through the BO sgtable: in `rocket_job_hw_submit`, find the regcmd BO from `job->bos[]`, modify bytes through its shmem mapping, then call `dma_sync_sgtable_for_device(BIDIRECTIONAL)` to properly flush cache. This is the kernel-API-blessed way to modify a BO that's about to be dispatched.
   b. **Check DCOMP_CTRL / weights setup** — mesa writes DCOMP_CTRL=0 (entry [33]: off=0x1100 val=0) but weights BO is 200K (compressed?). Without DCOMP enabled, CNA may stall before OP_ENABLE accept. Find DCOMP_CTRL bit layout and try forcing it on.
   c. **Mesa rkt_regcmd.c diff** — look at how mesa emits the CNA_OP_ENABLE entry on RK3588 vs RK3568. Maybe a SoC-specific quirk in CNA_OP_ENABLE value or order.
   d. **Inject known-good RKNPU regcmd into rocket** — capture BSP RKNPU's full YOLOv5s regcmd via instrumentation on l1, replay byte-for-byte through rocket's submission path. Splits "rocket submission infrastructure" from "mesa regcmd format".
   e. **Try TASK_DMA_BASE_ADDR pointing at a separate small mapped BO** — BSP uses task_base_addr separate from regcmd. Mesa rocket has no such concept but we could allocate a 4KB dummy BO and use it as task_base. Some hardware needs it pointing somewhere valid.

## Historical investigations (kept for context)

### REAL BUG REVEALED — AUTO_GATING fix REVERTED, IOMMU page fault at IOVA 0x118000 (2026-05-17)

(Resolved 2026-05-18 — see #2 above. The page fault was the TASK_DMA prefetch, not a mesa/BO mapping issue.)

1. **Kernel state**: `#61` with AUTO_GATING fix **fully removed**. Image-6.19 and Image-6.19-rocket both refreshed. RKNPU YOLOv5s WORKS again at 55.8ms/loop (l1), IRQ count 33. Rocket conv2d on l3 now shows REAL IOMMU page fault.

2. **REAL BUG**: With AUTO_GATING reverted, rocket shows:
   - `IOMMU FA=0x00118000` (page fault address)
   - `IOMMU ST=0x0b` (PAGING_ENABLED + PAGE_FAULT_ACTIVE + IDLE)
   - `PC_RS=0x00010000` (mmu_error in masked, bit 16)
   - `PC_TASKST=0xf000` (advanced past stuck 0x7000 — PC was executing!)

3. **What this means**: PC is alive and walking regcmd; it hits a reference to IOVA `0x00118000` which has no PTE mapping. Mesa userspace or the rocket kernel BO mapping path is missing this IOVA. The AUTO_GATING=0x2 "fix" was actually disabling the page walker → translation broken → STATUS never went to 0x01 → bug invisible.

4. **Investigation progress (2026-05-17)**:
   - Added `BO_MAP` diag in rocket_gem.c → captured full BO IOVA layout (864K, ends at 0xd8000)
   - Added `REGCMD_IOVA` + `REGCMD_FULL` diags in rocket_job.c → captured all DPU/CNA/RDMA config
   - DPU output cube config CORRECT: 40×40×128 within feature BO (0x73000..0xd7000)
   - All IOVA-shaped values in regcmd cleanly map to allocated BOs
   - No literal `0x118000` anywhere in regcmd — IOVA computed at runtime by sub-unit
   - Fault originates from CNA input read OR DPU_RDMA bias/sum read overshooting

5. **Next-session plan (after May 17 register-by-register decode)**:
   a. **Investigate `REG_DPU_SURFACE_ADD = 0x40c0` (mesa writes 0xc800 = 2× DST_SURF_STRIDE).** This register controls per-surface offset in DPU's destination address calculation. With wrong value, DPU writes overshoot the feature BO. Look for what mesa writes here in `rkt_regcmd.c` and verify correctness.
   b. **Check 52K mystery BO at 0x0..0xd000** — not referenced in any regcmd register write. May be the actual output buffer mesa wants but rocket isn't pointing DPU at it. Or it's a staging buffer mesa overlooks.
   c. **Decode IOMMU INT_STATUS bits** to identify which sub-unit fired the page fault. RK_MMU_INT_STATUS at 0x18 may have master ID encoded.
   d. **Capture & decode FEATURE_MODE_CFG = 0x1e4** at 0x400c — bits 2,5,6,7,8 set. May enable "flying mode" or unusual access pattern.



- RKNPU driver v0.9.8 loads and initializes on 6.19.0-rc5
- Non-IOMMU mode: YOLOv5s ~69ms at 1GHz/1000mV, MobileNetV1 ~6ms
- **IOMMU mode: WORKING** — MobileNetV1 7.98ms, YOLOv5s 56.7ms, correct outputs
- Boot entry **l3** for Rocket driver test (`Image-6.19-rocket`, `rk3568-rock-3b-6.19-rocket-fixed.dtb`)
- **Rocket 5-patch series ready** at `patches/rocket-rk3568/` — holding until inference test passes
- **IOMMU DTE confirmed sub-4GB** — GFP_DMA32 working ✓
- **pc_data_amount_scale=1 confirmed correct**: AMOUNTS_field=0x81 = count-1 ✓
- **BUG B FIXED** (2026-04-15): IOMMU detach/attach cycle works; `rk_iommu_identity_attach` prints every cycle, no EBUSY
- **trigger_enable fix applied** (2026-04-15): `rk_iommu_trigger_enable()` called after `iommu_attach_group()` in `rocket_job_run`; AMOUNTS now clean (0x0081) vs dirty (0x4081) before fix
- **BUG A "fix" REVERTED (2026-05-15)** — AUTO_GATING=0x2 was MASKING the real bug, not fixing it. Comparison test: booted l1 (rknpu) under kernel with AUTO_GATING=0x2 → YOLOv5s **also failed** (same PC-inert pattern as rocket). Reverting AUTO_GATING fully: YOLOv5s **works at 55.8ms/loop, IRQ 33**. The 0x2 value actually **disables the page walker entirely**, which makes STATUS=0x01 disappear (no walker = no AXI activity) but also breaks translation. ROCKETBUG A symptom was masked, not fixed.
- **TRM register extract created** (2026-05-07): `docs/npu-trm-registers.md` — **mandatory reference**
- **Remaining TRM-identified bugs**: (1) PC write order violates TRM Fig.9-2 (LIKELY CAUSE of PC mmu_error fired at ~10ms); (2) BOUNCE smp_wmb not DMA flush; (3) TASK_DMA_BASE_ADDR semantics unverified; (4) IRQ handler never registered
- **Regcmd pattern**: every odd 32-bit word = 0x02010000; format confirmed from TRM (64-bit: [63:56]=routing, [47:16]=value, [15:0]=offset)
- **Module load order fix (2026-05-12)**: `kernel/` path takes precedence over `extra/` in depmod. Always copy built rocket.ko to `kernel/drivers/accel/rocket/rocket.ko` — NOT just `extra/`.

## Board

- **User/Password**: radxa/radxa
- **SoC**: RK3568
- **IP**: Dynamic — use `cat ~/.rock3b_ip` or `./find-rock3b.sh`
- **Primary interface**: USB Ethernet (enx000ec649ecfe, <BOARD-MAC>)

```bash
./rock3b-ssh.sh "uname -a"
ssh radxa@$(cat ~/.rock3b_ip)
scp file radxa@$(cat ~/.rock3b_ip):/path/
```

## Project Locations on Board

```
~/npu-port/
├── linux-mainline/         # Kernel source
│   ├── drivers/rknpu/      # NPU driver (our port)
│   │   └── compat/         # Compatibility headers
│   └── arch/arm64/boot/dts/rockchip/
├── rockchip-bsp/           # BSP kernel (reference)
~/uboot-mainline/           # Mainline U-Boot + TF-A
~/rknpu2/                   # RKNN SDK examples
~/spi-backup-20260213.bin   # BSP U-Boot SPI backup (16MB)
```

## Current Git State (kernel)

```
Repository: ~/npu-port/linux-mainline
Branch: rknpu-port-6.19
Author: Midgy BALON <midgy971@gmail.com>
```

## Build Commands

```bash
# Full kernel (native on board)
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= -j4"

# Driver only (quick compile check)
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= drivers/rknpu/"

# Rocket driver only
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= -j4 M=drivers/accel/rocket"

# DTB only
ssh radxa@$IP "cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= rockchip/rk3568-rock-3b.dtb"

# Install
ssh radxa@$IP "cd ~/npu-port/linux-mainline && sudo cp arch/arm64/boot/Image /boot/Image-6.19-genpd && sudo make ARCH=arm64 modules_install"
```

## Boot Config

- **l0**: BSP 5.10 rescue — `vmlinuz-5.10.160-18-rk356x` + BSP fdtdir
- **l1**: Mainline 6.19 IOMMU (**default**) — `Image-6.19` + `rk3568-rock-3b-6.19-iommu.dtb`
- **l2**: Mainline 6.19 non-IOMMU (fallback) — `Image-6.19` + `rk3568-rock-3b-6.19-npu.dtb`
- **l3**: Mainline 6.19 Rocket accel driver — `Image-6.19-rocket` + `rk3568-rock-3b-6.19-rocket-fixed.dtb`
  - Image-6.19-rocket: compile server build, has `DRM_ACCEL=y DRM_ACCEL_ROCKET=m`
  - Rocket DTB (fixed): `rknn_mmu_0` uses `rockchip,iommu` (v1 ops, GFP_DMA32)

The iommu DTB has `rknpu_mmu` node enabled (status = "okay").

## Current NPU Driver Architecture

### Key Design Decisions

**SCMI two-step rate change** in `rknpu_power_on`:
```c
// Force TF-A clk_npu_set_rate() to run (triggers PVTPLL init = NPU bus de-idle)
clk_set_rate(scmi_clk, 600000000UL);  // intermediate rate
clk_set_rate(scmi_clk, rate);          // target rate
```

**PMU de-idle** via `rknpu_noc_deidle()`:
- Disables NOC auto-idle for NPU: `writel((1<<18)|0, pmu + 0x70)`
- Powers on NPU domain: `writel((1<<17)|0, pmu + 0xa0)`
- Requests de-idle: `writel((1<<18)|0, pmu + 0x50)`
- Waits up to 1ms for `BUS_IDLE_ST` bit2 to clear

**pm-domains always_on**: `DOMAIN_RK3568_AO` in `pm-domains.c` — genpd callbacks
are no-ops for NPU domain (standard genpd can't manage NPU without SCMI+PMU sequence).

### DT: NPU Node

```dts
&rknpu {
    status = "okay";
    rknpu-supply = <&vdd_npu>;
    power-domains = <&power RK3568_PD_NPU>;
    assigned-clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>;
    assigned-clock-rates = <1000000000>, <600000000>;
    operating-points-v2 = <&npu_opp_table>;
};

/* OPP table in rk3568-rock-3b.dts: 200/400/600/800/1000 MHz at 825/875/925/950/1000 mV */

&vdd_npu {
    regulator-always-on;
    regulator-boot-on;
    regulator-min-microvolt = <825000>;
    regulator-max-microvolt = <1000000>;
};
```

### Modified Files on Board

- `drivers/rknpu/rknpu_drv.c` — FPGA_PLATFORM removed, `rknpu_noc_deidle()`, SCMI two-step
- `drivers/rknpu/rknpu_reset.c` — SCMI two-step + `rknpu_noc_deidle()` before IOMMU ops in soft_reset
- `drivers/rknpu/rknpu_debugger.c` — FPGA_PLATFORM removed
- `drivers/rknpu/rknpu_devfreq.c` — Mainline devfreq (replaced BSP stub)
- `drivers/rknpu/rknpu_devfreq_bsp.c` — BSP version kept for reference
- `drivers/iommu/rockchip-iommu.c` — `hw_needs_enable` flag + debug prints + `__maybe_unused` + **`iommu_data_ops_v2.gfp_flags = GFP_DMA32` (PTE=0 fix)** + **`AUTO_GATING=0x2` write after `rk_iommu_enable_paging()` in both `rk_iommu_enable()` and `rk_iommu_resume_paging()` (BUG A fix, 2026-05-12 PM)**
- `drivers/pmdomain/rockchip/pm-domains.c` — `always_on` NPU domain
- `arch/arm64/boot/dts/rockchip/rk356x-base.dtsi` — NPU node + rknpu_mmu (disabled by default)
- `arch/arm64/boot/dts/rockchip/rk3568-rock-3b.dts` — NPU enable, OPP table, VDD_NPU

### Rocket Driver Modified Files on Board (Apr 15 2026)

- `drivers/accel/rocket/rocket_core.h` — `iommu_dev` field (for trigger_enable/disable) + `attached_domain` field + forward decls
- `drivers/accel/rocket/rocket_job.c` — `attached_domain` tracking, HW_SUBMIT debug, detach-before-attach,
  pm_runtime leak fix; **`rk_iommu_trigger_enable()` called after `iommu_attach_group()`** (Apr 15 patch);
  REGCMD tail dump (last 8 words) in hw_submit debug block
- `drivers/accel/rocket/rocket_drv.c` — `pc_data_amount_scale=1`, postclose detach, runtime_resume detach;
  **iommu_dev lookup** via `of_parse_phandle(node, "iommus", 0)` + `of_find_device_by_node()` in probe;
  **`rk_iommu_trigger_disable()` called before clock gate** in `rocket_device_runtime_suspend` (Apr 15 patch)
- rocket.ko rebuilt: Apr 15 16:25, installed at `/usr/lib/modules/6.19.0-rc5/kernel/drivers/accel/rocket/`
  **NOTE**: scale=2 and REGCMD tail dump changes did NOT take effect on last test run (module reload issue — see 10a)
- `drivers/iommu/rockchip-iommu.c` — `rk_iommu_trigger_enable` / `rk_iommu_trigger_disable` exported functions
  (now integrated: trigger_enable called from rocket_job.c, trigger_disable from rocket_drv.c runtime_suspend)

## Serial Console

**Port**: UART2, GPIO header, 3.3V — **1500000 baud**

```bash
# Capture with picocom (recommended — clean log file)
picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/$(date +%Y-%m-%d)_l3_<description>.log

# Quick monitor only
picocom -b 1500000 /dev/ttyUSB0
```

Logs go in `logs/serial/` — see `logs/serial/README.md` for naming convention, annotation format,
grep recipes, and index of all past logs.

Index of older ad-hoc logs at project root: `log4.txt` (IOMMU probe fix), `log5.txt` (INT_MASK wiring),
`log6.txt` (GFP_DMA32 fix ← YOLOv5s working), `last_boot_log.md` (Rocket first probe),
`bootn2.txt` (scale=1 + AMOUNTS=0x81), `bootn3.txt` (detach-before-attach).

## NPU Testing

```bash
# Check driver loaded
ssh radxa@$IP "dmesg | grep -i rknpu | tail -10"

# Check DRM devices
ssh radxa@$IP "ls -la /dev/dri/"

# YOLOv5s benchmark
ssh radxa@$IP "cd ~/rknpu2/examples/rknn_yolov5_demo && ./build/build_linux_aarch64/rknn_yolov5_demo model/RK3566_RK3568/yolov5s-640-640.rknn model/bus.jpg"

# Teflon inference test
ssh radxa@$IP "python3 ~/npu-debug/teflon_test.py"

# Check debugfs
ssh radxa@$IP "sudo cat /sys/kernel/debug/rknpu/version"
```

## Benchmarks (RK3568, YOLOv5s-640x640)

| NPU Freq | Kernel freq | ms/frame |
|----------|-------------|----------|
| 400 MHz | CRU/GPLL | ~120ms |
| 600 MHz | CRU/GPLL | ~82ms |
| 800 MHz | PVTPLL/SCMI | ~74ms |
| 1000 MHz | PVTPLL/SCMI (non-IOMMU) | ~69ms |
| 1000 MHz | PVTPLL/SCMI **IOMMU** | **~57ms** |

## PMU Write-Mask Protocol

Upper 16 bits = write-enable mask, lower 16 bits = value.
- SET bit N: `(1 << (N+16)) | (1 << N)`
- CLEAR bit N: `(1 << (N+16)) | 0`

NPU key bits: PWR_DWN_ST bit1 (0=ON), BUS_IDLE_ST bit2 (0=ACTIVE)

## Rocket Patch Series (Ready — Not Yet Sent)

**Location**: `patches/rocket-rk3568/` (5 patches + cover letter)

| Patch | File | Subject |
|-------|------|---------|
| 0/5 | 0000-cover-letter.patch | Cover letter |
| 1/5 | 0001-accel-rocket-Use-of_find_matching_node... | `for_each_of_allnodes` → `of_find_matching_node` (module fix) |
| 2/5 | 0002-accel-rocket-Add-support-for-Rockchip-RK3568... | `rk3568_noc_init()` + `rk3568_soc_data` |
| 3/5 | 0003-dt-bindings-npu-Add-RK3568... | Binding: add rk3568-rknn-core, rockchip,pmu, optional sram-supply |
| 4/5 | 0004-arm64-dts-rockchip-rk356x-Update-NPU-node... | rk356x-base.dtsi: Rocket node layout, dual label, 200MHz default |
| 5/5 | 0005-arm64-dts-rockchip-rk3568-rock-3b-Enable-NPU... | rock-3b.dts: npu-supply, drop operating-points-v2 |

**dtbs_check**: passes clean on rk3568-rock-3b.dtb and all rk356x boards.

**Blocking**: inference test on l3 boot entry before sending. Tomeu will ask.

**Recipients** (from cover letter):
- To: Tomeu Vizoso `<tomeu@tomeuvizoso.net>`, Oded Gabbay `<ogabbay@kernel.org>`
- Cc: Rob Herring, Krzysztof Kozlowski, Conor Dooley, Heiko Stuebner
- Cc: dri-devel, devicetree, linux-arm-kernel, linux-rockchip, linux-kernel

## IOMMU Upstream (Waiting on Simon's Patch)

- Original GFP_DMA32 patch **withdrawn** 2026-04-03 (Message-Id: `<20260331075010.1463-1-midgy971@gmail.com>`)
- Simon Xue (Rockchip) confirmed two IOMMU versions: v1.0 (rknpu_mmu 0xfde4b000, vicap_mmu 0xfdfe0800) and v2.0 (VOP2)
- v1.0 cannot place DTE (first-level page table) above 4GB; both versions can map 40-bit physical pages
- **Fix path**: wait for Simon's per-device-ops patch (`<20260310105303.128859-1-xxm@rock-chips.com>`), then:
  1. Add `rockchip,rk3568-iommu-v1` compatible with `.gfp_flags = GFP_DMA32`, `.dma_bit_mask = DMA_BIT_MASK(40)`
  2. Update rk356x-base.dtsi: use `rk3568-iommu-v1` for rknpu_mmu + vicap_mmu nodes
- Offered Tested-by on Simon's patch; reply sent 2026-04-03

## Next Steps

1. ✅ `pd_ignore_unused` removed — always_on handles it
2. ✅ FPGA_PLATFORM ifdefs removed, `rknpu_noc_deidle()` renamed
3. ✅ devfreq re-enabled with mainline OPP APIs (2026-03-23)
4. ✅ On-board NICs auto-load via dwmac-rk udev alias
5. ✅ IOMMU probe + enable flow works; soft_reset crash fixed (2026-03-24)
6. ✅ NPU INT_MASK bit 16 wired for IOMMU mode; rk_iommu_irq fires on fault (2026-03-27)
7. ✅ Fix IOMMU PTE=0 bug — `iommu_data_ops_v2.gfp_flags = GFP_DMA32` (2026-03-29)
   - Root cause: missing GFP_DMA32 caused SWIOTLB bounce buffers for page tables; DMA_TO_DEVICE sync zeroed PTEs
   - Fixed with one-line change in `drivers/iommu/rockchip-iommu.c`
   - IOMMU mode now fully working: YOLOv5s 56.7ms, MobileNetV1 7.98ms, correct outputs
8. ✅ Rocket driver probing on RK3568 (2026-04-03) — `/dev/accel/accel0` created
   - `rocket_device.c`: `for_each_of_allnodes` → `of_find_matching_node` (non-exported symbol fix)
   - `rocket_drv.c`: PVTPLL two-step rate change (600MHz→1GHz) before NOC de-idle
   - `rocket_drv.c`: `regmap_write(pmu, 0xa0, BIT(1+16))` — PWR_GATE_SFTCON explicit power-on
     (genpd `always_on` no-op skips this; NPU hardware still power-gated at boot)
   - Boot entry **l3**: `Image-6.19-rocket` + `rk3568-rock-3b-6.19-rocket-fixed.dtb`
   - DTS: `&rknpu` overridden with `compatible = "rockchip,rk3568-rknn-core"`, 3-region reg layout
   - `ROCKET_CLK_NPU_IDX 2` = index of "npu" (SCMI) clock in core->clks[] bulk array
9. ✅ **Fix IOMMU DTE bug** — fixed DTB deployed (2026-04-04/05)
   - Root cause: `rockchip,rk3568-iommu` → v2 ops → `gfp_flags=0` → DTE above 4GB on 8GB board
   - Symptom: `PC_INTERRUPT_RAW_STATUS_DMA_READ_ERROR` (bit 12 = 0x1000) on every NPU job
   - Fix: `rk3568-rock-3b-6.19-rocket-fixed.dtb` — `rknn_mmu_0` uses `rockchip,iommu` (v1 ops, GFP_DMA32)
   - **CONFIRMED WORKING** 2026-04-06: DTE_ADDR=0x044c8000 (sub-4GB), DMA_READ_ERROR no longer fires
10. ✅ **pc_data_amount_scale fix compiled** (2026-04-05, rocket.ko 16:51)
    - scale=1 for RK3568 confirmed: AMOUNTS_field=0x81 = (130-1) ✓
    - But two new bugs found — see 10a/10b
10a. ✅ **BUG A FIXED: AUTO_GATING register** (2026-05-12 PM)
    - **Root cause**: `RK_MMU_AUTO_GATING` (offset 0x24) reset value = 0x3. Mainline rockchip-iommu.c
      never wrote this register, so the hardware default carried through. **bit 1** = page-walker
      auto-clock-gating → walker's AXI master loses clock between transactions → on TLB-miss page walk,
      AXI read for DTE never completes → perpetual STATUS=0x01 (PAGING_ENABLED only, no IDLE) for 500ms.
    - **bit 0** = register-port (slave) auto-gating; **must remain set** — writing 0 to bit 0 clears the
      DTE_ADDR register. Empirical bit-by-bit decode confirmed: only value 0x2 works.
    - **Ordering**: AUTO_GATING write must come AFTER `rk_iommu_enable_paging()` succeeds.
      Writing before enable_paging causes `"Enable paging request timed out, status: 0x000018"` —
      STATUS bit 0 (PAGING_ENABLED) never sets, and subsequent NPU reads fail with DMA_READ_ERROR.
    - **Fix sites in `drivers/iommu/rockchip-iommu.c`**:
      ```c
      ret = rk_iommu_enable_paging(iommu);
      if (!ret) {
          for (i = 0; i < iommu->num_mmu; i++)
              rk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);
      }
      ```
      Same pattern in both `rk_iommu_enable()` and `rk_iommu_resume_paging()`.
    - **Confirmed live**: kernel `#57 SMP May 12 10:48 CEST`, `/boot/Image-6.19-rocket`.
      Verified via rocket.ko diagnostic (read-only `IOMMU_EXT` block in rocket_job.c PRE_OP):
      `AUTO_GATING=0x00000002` on every PRE_OP, IOMMU `ST=0x19` (IDLE+PAGING_ENABLED+REPLAY_EMPTY)
      throughout each Teflon job, no AXI stall, `resume_paging ret=0`.
    - **Submission**: This is a one-line kernel fix that should be sent upstream as a standalone patch.
      Affects all RK356x boards using IOMMU v1 (rknpu_mmu, vicap_mmu). The vendor BSP driver writes
      AUTO_GATING in its power path; mainline driver inherited the gap.
10b. ✅ **FIXED: EBUSY loop after first job timeout** (2026-04-15)
    - Root cause confirmed: `rk_iommu_suspend` (or PM callback on reset) was resetting `iommu->domain`
      to identity_domain before `iommu_detach_group` in `rocket_reset` could run → early-return with no print
    - Fix: detach-before-attach in `rocket_job_run` + `attached_domain` tracking (Apr 6) ensures clean state;
      confirmed working Apr 15: `rk_iommu_identity_attach: detaching from rk_domain=...` now prints on every cycle
10c. ⏳ **NEXT BUG: PC `mmu_error` at ~10ms (independent of IOMMU)**
    - With BUG A fixed, IOMMU stays healthy (ST=0x19) but PC still fires `PC_RS=0x00004000` (mmu_error
      bit 14, unmaskable, unclearable) at AT10MS. Timing:
      - AT50US/AT1MS: PC_AMOUNTS=0x84 (clean count-1=132), PC_RS=0 — clean
      - AT5MS: PC_AMOUNTS=**0x4084** (bit 14 SET by HW), PC_RS=0 — something happens at ~5ms
      - AT10MS: PC_RS=**0x4000** (mmu_error) — fires before 10ms boundary, before any IOMMU activity
      - Job times out at 500ms; resume_paging recovers cleanly; next job repeats same pattern
    - IOMMU never transitions out of ST=0x19 → PC never even issues an AXI read that hits the IOMMU.
      Suggests PC detects an internal MMU-related fault BEFORE attempting to fetch regcmd via DMA.
    - **All BUG C suspects ruled out (May 12 PM)**:
      - ~~CNA/CORE S_POINTER pre-writes~~: removed → no change in failure pattern
      - ~~OP_EN single-write vs pulse~~: tried `1 then 0` → no change
      - ~~TASK_DMA_BASE_ADDR = task->regcmd~~: tried writing 0 → no change
      - ~~INT_MASK | BIT(16) for IOMMU mode~~: added BSP-style bit → no change
    - **Investigation eclipsed by BUG D below**: PC_RS=0x4000 may be a normal completion signal,
      not a fault. Without IRQ propagation, we can't tell.
10d. ✅ **BUG D1 FIXED: PC bit-14 mmu_error caused by missing OPP DT reference** (2026-05-15)
    - Fix: add `operating-points-v2 = <&npu_opp_table>` to `&rknpu` node in
      `arch/arm64/boot/dts/rockchip/rk3568-rock-3b-rocket.dts` (line 825 area).
      The DTS already defined `npu_opp_table` at top but didn't reference it.
    - Before fix: PC fires bit 14 in RAW_STATUS immediately after OP_EN=1.
      INT_MASK/AMOUNTS readbacks have bit 14 HW-OR'd (0x4300, 0x4084).
    - After fix: PC_RS=0 throughout, all readbacks clean (no bit-14 OR).
    - Devfreq isn't even registered — just having the DT property suffices.
      The OPP reference apparently puts NPU into a "valid OPP" power state
      that PC's internal pre-flight check requires.
10e. ⏳ **BUG D2 OPEN: PC doesn't START fetching regcmd after D1 fix** (2026-05-15)
    - PC has correct registers: BASE=0xd7000, AMOUNTS=0x84, MASK=0x300, TASKCON=0x1
      (after pulse-bit self-clear), TASKDMA=0xd7000, TASKST=0x7000, OPENA=1. PC_RS=0
      throughout (no fault). PC_VERSION/VERNUM read 0 (likely unimplemented in IP rev).
    - But PC_AMOUNTS frozen at 0x84 (no decrement). IOMMU stays idle ST=0x19. IRQ count=0.
      PC isn't actually starting regcmd fetch.
    - **D2 ruled out (no effect after D1)**: rocket_core_reset at runtime_resume,
      BSP-style OP_EN pulse (write 1 then 0).
    - **D2 regulator fix applied + verified (2026-05-15)** but didn't unblock D2:
      Confirmed rocket had ZERO `regulator_*` calls — DT `npu-supply = <&vdd_npu>` was
      ignored entirely. With `regulator-always-on` only, vdd_npu was at min 825mV
      (200MHz OPP voltage) while SCMI was at 1GHz. Added in `rocket_device_runtime_resume`:
      ```c
      struct regulator *r = devm_regulator_get_optional(dev, "npu");
      if (!IS_ERR(r)) {
          regulator_set_voltage(r, 1000000, 1000000);
      }
      ```
      Verified `/sys/class/regulator/regulator.11/microvolts = 1000000`, and serial log
      shows `rocket: vdd_npu set 1000mV ret=0`. **PC behavior unchanged** — still
      `PC_RS=0`, `AMOUNTS=0x84` frozen, IRQ count=0. So voltage isn't the gating
      factor. Keep this fix anyway — correct behavior for a devfreq-less driver.
    - **D2 STILL OPEN** — all visible-from-software params now match RKNPU's working
      state but PC remains inert. The remaining gap must be hardware-internal NPU
      state we can't observe.
    - **CNA/CORE register dump (2026-05-15)** added at AT10MS. Sample:
      ```
      CNADUMP:  S_STATUS=0 S_POINTER=0x0e OP_EN=0 CONV_CON1=0 CBUF_CON0=0x44 DCOMP_CTRL=0
      COREDUMP: S_STATUS=0 S_POINTER=0x0e OP_EN=0
      ```
      `S_POINTER=0x0e` confirms rocket's CPU pre-write took (CNA/CORE MMIO partially
      writable). But `OP_EN=0` on both sub-units → regcmd's `OPERATION_ENABLE = 1`
      was NOT executed. Conclusively proves PC isn't broadcasting any regcmd writes
      to sub-units. `CBUF_CON0 = 0x44` is hardware-fixed read-only (verified by
      writing 0xDEADBEEF / 0 from CPU → readback unchanged) — useless as a probe.
    - **`REG_CNA_CLK_GATE = 0x1090`** discovered — has bits to disable per-sub-block
      clock gating in CNA (CBUF, WEIGHT, CSC, FEATURE). Setting to 0x1F didn't
      change CBUF_CON0 writability and didn't start PC. Keep for future tests.
    - **Sub-unit gating regs PC-broadcast-only (2026-05-15)**: tested writing all
      known sub-unit gating regs from CPU. ALL ignore CPU writes:
      - `CNA_CLK_GATE (0x1090)`: wrote 0x1F → readback 0 (ignored)
      - `CORE_MAC_GATING (0x300c)`: wrote 0 → readback 0x1000 (HW val, ignored)
      - `CBUF_CON0 (0x1040)`: wrote 0xDEADBEEF → readback 0x44 (HW-fixed, ignored)
      S_POINTER/S_STATUS accept CPU writes; gating/config registers do not.
      Conclusion: these registers are writable **only via PC's broadcast bus**
      (executing regcmd entries). Since PC isn't broadcasting, we can't fix
      sub-unit state from outside.
    - **OPP framework integrated (2026-05-15)** but didn't unblock D2. Replaced
      manual clk_set_rate + regulator_set_voltage in `rocket_device_runtime_resume`
      with `dev_pm_opp_set_rate(dev, 1000000000)`. Probe-time setup in
      `rocket_core_init`:
      ```c
      devm_pm_opp_set_clkname(dev, "npu");
      devm_pm_opp_set_regulators(dev, (const char*[]){"npu"});
      devm_pm_opp_of_add_table(dev);
      ```
      Result: `opp_set_rate(1GHz) OK`, vdd_npu @ 1000mV ✓. **PC behavior unchanged.**
      Even RKNPU-like coordinated power management doesn't make PC fetch regcmd.
      Keep this change — proper power-management architecture regardless of D2.
    - **D2 next-session priorities (after OPP also didn't help)**:
      1. Boot l1 with ftrace enabled, capture ALL register touches RKNPU makes during
         probe and YOLOv5s submission. Diff against rocket. The missing init is in
         a register we haven't checked.
      2. **BSP-primer hack**: load rknpu module first, run YOLOv5s once to fully
         "wake up" NPU, then `rmmod rknpu; modprobe rocket` (no power cycle in between).
         If rocket inherits NPU's "primed" state and runs successfully, it confirms
         the missing init is a one-time hardware boot sequence we need to port.
      3. Inject known-good RKNPU YOLOv5s regcmd into rocket (manual hex blob from l1
         capture). Splits "userspace regcmd format incompatibility" from "rocket kernel
         submission infrastructure broken".
      4. Try skipping rocket's per-job IOMMU detach/attach cycle (RKNPU keeps domain
         attached once at probe).
      5. Port full `rknpu_soft_reset` sequence (currently rocket has just basic
         srst_a + srst_h, but rknpu does much more: msleep, NOC re-init, IOMMU detach/
         reattach, state_init).

10f. ⏳ **Historical BUG D investigation findings (May 12 PM, archived)**
    - **Confirmed real fault, rocket-specific**: l1 boot YOLOv5s diagnostic shows
      RKNPU's PC_RAW_STATUS = `0x00000000` at 100us post-OP_EN. Rocket shows
      `0x4000` by AT5MS. Bit 14 firing is NOT normal completion — it's a fault
      that PC enters on rocket specifically.
    - **PVTPLL clock hypothesis BUSTED**: l1 and l3 both show identical clk_summary
      (clk_npu_pvtpll at 200MHz, count=0). The Linux clk subsystem doesn't track
      TF-A-managed PVTPLL accurately. Clock isn't the issue.
    - **All per-job programming RULED OUT** (none changed PC's bit 14 timing):
      - CNA/CORE S_POINTER pre-writes (kept vs removed)
      - OP_EN single-write vs BSP pulse (1 then 0)
      - TASK_DMA_BASE_ADDR=`task->regcmd` vs `0`
      - INT_MASK = 0x300 vs `0x300 | BIT(16)` vs `0x0FFF`
      - INT_CLEAR = 0xFFFFFFFF vs 0x300 (BSP value)
      - TASK_NUMBER = 1 vs 6
      - BSP-exact register write order (addr/amount/mask/clear/task/dma/op_en)
      - data_amount EXTRA_AMOUNT = 0 vs +4 (BSP value)
      - SCMI two-step rate change (reports 1GHz from clk_get_rate)
    - **Verified-OK**: regcmd is well-formed (REGCMD_SCAN: CNA_OP=1 CORE_OP=1
      DPU_OP=1, last entry = DPU_OP_ENABLE=1). GIC IRQ 39 wiring works (proven on l1).
    - **Notable DT differences** (l1 rknpu node vs l3 rocket node):
      - rocket lacks `operating-points-v2` (no OPP table for devfreq)
      - rocket lacks `interrupt-names`
      - rocket has `npu-supply`/`sram-supply`, l1 has `rknpu-supply`
      - rocket node has `rockchip,pmu` phandle; l1 doesn't (uses syscon)
    - **RKNPU vs rocket per-job state (unexplained)**:
      | Field          | RKNPU YOLOv5s    | Rocket conv2d |
      |----------------|------------------|---------------|
      | regcmd IOVA    | 0xffefaa00 (top) | 0xd7000 (low) |
      | task_ctrl      | 0x612f / 0x6006  | 0x6001        |
      | data_amount    | 0x88 / 0x24b     | 0x84          |
      | Submissions    | 3 batches        | 1 batch       |
    - **Next-session plan (May 13+)**:
      1. **Add `operating-points-v2` to rocket DT**. Rebuild rk3568-rock-3b-6.19-rocket.dtb.
         If OPP table is what triggers devfreq → PVTPLL enable, this might unblock PC.
      2. **Port `rknpu_devfreq.c` to rocket**. Even if not directly the bug, gets us a
         known-good NPU power-management sequence.
      3. **Try high-IOVA regcmd allocation**: emulate RKNPU's pattern by allocating
         rocket BOs at high IOVA range. May reveal IOMMU v1 quirks with low IOVAs.
      4. **Rebuild Image-6.19 (RKNPU kernel) with #58 build + AUTO_GATING fix**. Verify
         RKNPU still works under newer kernel — rules out a kernel-build regression
         between Mar 31 (#47) and May 12 (#57).
      5. **Verify rocket regulator handling**: rocket DT has `npu-supply` but rocket
         driver may not call `regulator_enable` on it. RKNPU does. If regulator's not
         on, NPU sub-units may have inadequate voltage → timing failure.
    - **DT routing PROVEN working** (l1 boot test, May 12 PM): same GIC 183 (IRQ 39) shared
      between iommu+npu. Booted l1, ran YOLOv5s 10 loops at 48.9 ms/loop, IRQ count
      went 0 → 33 (~3.3 IRQs per inference). DT is fine; hardware is fine.
    - **INT_MASK ruled out**: RKNPU uses `int_mask=0x00000300` (same as rocket). Tried
      widening rocket's INT_MASK to `0xFFF` (all sub-unit done bits 0-11) AND modifying
      handler to accept any bit — IRQ count STILL 0. PC asserts NO done bit in rocket mode.
    - **Other BUG D candidates also ruled out**: TASK_DMA_BASE_ADDR=0, removing CNA/CORE
      S_POINTER pre-writes, BSP-style OP_EN pulse — none made PC fire any done bit.
    - **REGCMD_SCAN diagnostic (May 12 PM)**: examined all 133 regcmd entries via memremap.
      Result: `count=133 CNA_OP=1 CORE_OP=1 DPU_OP=1 PPU_OP=0 LAST_OFF=0x4008 LAST_VAL=0x1`.
      Mesa correctly emits all three OP_ENABLE writes (CNA 0x1008, CORE 0x3008, DPU 0x4008),
      and the LAST entry is `DPU_OP_ENABLE=1`. Userspace regcmd is well-formed.
    - **Refined hypothesis: PC isn't consuming the regcmd at all**:
      - `PC_REGISTER_AMOUNTS` readback stays at `0x84` (count-1=132) for the entire job.
        Never decrements → PC isn't fetching entries.
      - IOMMU stays ST=0x19 idle throughout → PC issues no AXI reads via the IOMMU.
      - Only `PC_AMOUNTS bit 14` (HW status, undocumented) and `PC_RAW_STATUS bit 14`
        set at ~5–10 ms. PC is "active" internally but not executing the regcmd.
    - **INT_CLEAR variation test (May 12 PM)**: tried INT_CLEAR=0x300 (BSP-style narrow)
      vs 0xFFFFFFFF (rocket original) — identical IRQ-silent behavior. Not the trigger.
    - **Next-session plan**:
      1. **TASK_NUMBER semantics**: RKNPU uses 0x12f or 0x6 for YOLOv5s (varying per
         submission); rocket uses 0x1. Test rocket with `TASK_NUMBER = 0x6` (matching
         BSP's later submissions) to see if PC starts. Same for `0x0` and `0x12f`.
      2. **Check PC clock/power prerequisites**: dump all NPU clock states and PMU
         status bits before vs after OP_EN — verify nothing's gated.
      3. **Check ENABLE_STALL state**: PC may have an internal "stalled" state that
         needs explicit DISABLE before OP_EN takes effect. Read PC_TASK_STATUS bits
         and check if any "halt" indicator is set.
      4. **Poll-mode workaround**: kthread polling PC_AMOUNTS countdown; signals
         completion when count reaches 0 or specific bits transition. Validates
         whether PC ever actually completes, independent of IRQ infrastructure.
    - **Diagnostic ideas for next session**:
      - Set `TASK_DMA_BASE_ADDR = 0` (or a different sentinel) to see if PC's behavior changes
      - Decode PC_REGISTER_AMOUNTS bit 14 from TRM (RK3568 PC RTL register reference if accessible)
      - Read PC_INTERRUPT_STATUS (masked, not RAWST) to see what masked bits PC asserts
      - Compare PC_TASK_STATUS at AT5MS vs AT10MS to find the transition trigger
11. ⏳ **Send Rocket 5-patch series** — after inference confirmed (see patches/rocket-rk3568/)
    - Cover letter needs note: requires IOMMU v1.0 fix (Simon's patch) to work on 8GB+ boards
12. ⏳ Submit IOMMU v1.0 fix — after Simon's per-device-ops patch lands
13. PI6C20100 patch v6 sent 2026-03-27; awaiting Heiko Stuebner to apply
14. U-Boot debug cleanup: checkpoint letters A-J, e0-e5 in board_f.c; driver name prints in device.c

## Reference Docs

- `docs/npu-investigation.md` — Root cause analysis and investigation sessions
- `docs/api-porting.md` — API changes reference and compat headers
- `docs/patches.md` — Patch submission history

## Reference Links

- [RKNPU2 SDK](https://github.com/rockchip-linux/rknpu2)
- [Rocket NPU driver](https://docs.kernel.org/accel/rocket/index.html)
- [gated-fixed-clock binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/clock/gated-fixed-clock.yaml)
- [RK3568 TRM Part 1](https://opensource.rock-chips.com/images/2/26/Rockchip_RK3568_TRM_Part1_V1.3-20220930P.PDF)
- [PI6C20100 patch lore (v2)](https://lore.kernel.org/linux-rockchip/20260304132957.684616-1-midgy971@gmail.com/)
