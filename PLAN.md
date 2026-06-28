# ROCK 3B (RK3568) NPU — Rocket Driver Bring-up: Living Plan

> **Living document.** Updated at the END of each session. Top = current truth and
> next steps; bottom = dated session log. If you're a future session: read
> **PROVEN FACTS** and **RULED OUT** first — do NOT re-test those. Then go to
> **NEXT STEPS**.

Last updated: **2026-06-07**

---

## 0. Goal / Definition of Done

Make the mainline **`rocket`** accel driver run a real convolution on the RK3568
NPU (ROCK 3B), then upstream it.

- **Litmus for "it works":** `conv2d.tflite` via Teflon (`~/npu-debug/teflon_test.py`,
  all-ones input) produces `out_sum == 23,252,430` (CPU reference). Current broken
  value: **`26,214,400`** (= 204800 × 128, i.e. output uniformly = zero_point 128).
- **HW-level success probes:** CNA `CBUF_CON0` HW bit-13 sets (→ `0x2044`/`0x2017`);
  a completion **IRQ fires**; IOMMU does real read+write traffic then returns to idle.
- **End game:** once it works on this dev kernel (6.19.0-rc5 #65), retest on the
  latest *stable* mainline, then submit the v3 driver+DT series. See
  `memory/maintainer_feedback_v2_2026_05_29.md` and
  `upstream-prep/PRE_SUBMISSION_CHECKLIST.md`.

---

## 1. Current Status (TL;DR)

**2026-06-09 ✅✅ rocket=m RESOLVES the need_regulator deadlock → v4 keeps patches 9+10.**
Chaoyi was right: the 2026-06-08 RCU stall was an artifact of `rocket=y` (built-in → early-initcall
probe → genpd→I2C-PMIC regulator op before RCU/scheduler ready). Rebuilt 7.1-rc6 + Simon + clean v4
(1-10, **need_regulator** variant: `DOMAIN_M_R need_regulator=true` + `domain-supply=<&vdd_npu>`, no
always-on) + **`CONFIG_DRM_ACCEL_ROCKET=m`**, deployed as boot entry **l6**. Booted on serial: **boots
clean, teflon runs 5 jobs, NO RCU stall / NO hang** (rocket autoloads @6.84s post-udev). The
`Failed to create device link 0-0020` warning Chaoyi asked about prints at 2.75s during pmdomain probe
(independent of rocket; non-fatal, rail up @825mV). So **v4 KEEPS need_regulator (drops the always-on
hack)**; `=m` is upstream-normal anyway. Open follow-up (disclosed to Chaoyi): the device-link warning
+ `fw_devlink=permissive` reliance — benign ordering or a clean fix? out_sum still 26214400 = the
separate compute gate (unchanged). Reply ✅ SENT 2026-06-09 (boot log attached):
`upstream-prep/chaoyi-rocket-m-result-reply.md` — awaiting Chaoyi/pmdomain on the device-link question.
Evidence: `logs/serial/2026-06-09_rocket-m-needreg{.log,.ANNOTATED.md}`. Memory:
`chaoyi_rockchip_feedback_2026_06_05.md`. v4 spec updated. (Board on l3 default; l6 = this test kernel.)
**Also verified 2026-06-09: `v4-0008` (rk356x dts) carries the CRU `CLK_NPU` assigned-clocks fix**
(`assigned-clocks = <&scmi_clk SCMI_CLK_NPU>, <&cru CLK_NPU>` @ 200M/600M; aclk_npu 12→200 MHz, the
2026-06-07 latent bug), documented in its commit msg. ⇒ the on-disk `upstream-prep/v4-patches/`
(10 patches, **need_regulator** variant) is now content-correct per every decision to date
(Simon per-device IOMMU ops, Chaoyi AUTO_GATING bit-31 = patch 6, need_regulator = patches 9+10,
CLK_NPU = patch 8, Sashiko bot fixes). Still RFC / NOT sent — pending human maintainer replies on
the rest of the series + the compute gate; standing rule = no send/push until maintainers OK.

**2026-06-04 (later) 🟢 CLEAN DT-ONLY BRING-UP — scaffolding obsolete.** The initcall hack +
genpd no-op (items 4 & 5 below) are NO LONGER NEEDED. Stock `DOMAIN_RK3568` genpd de-idles the
NoC at power-on (when the IOMMU supplier resumes); it just needs `vdd_npu` up — **NOT PVTPLL**
(the 2-yr "PVTPLL before de-idle" assumption was wrong; PVTPLL is only for compute, set later in
the driver). The fix is **DT-only** (branch `rocket-rk3568-v3-genpd` @564cb8a, +5/-3 vs v3):
(1) drop `pm_qos` from the NPU power-domain child (qos_npu is behind the gated NoC → genpd
power-off save_qos faults); (2) `vdd_npu regulator-always-on/boot-on` (min 825mV) so the rail is
up before genpd de-idles. NPU probes on v7.1-rc6, no abort, no hang, boots to login. **Do NOT use
genpd `domain-supply` for vdd_npu — it deadlocks** (device-link to the I2C PMIC → RCU stall);
always-on is the answer. Conv still uniform-128 (compute gate). New downstream noise on each job
timeout: `WARNING iommu.c:157 __iommu_group_set_core_domain` via `rocket_reset→iommu_detach_group`
(-110 IOMMU stall timeout) + `rk_iommu ... stall ... timed out 0x19`. See
`memory/genpd_clean_bringup_2026_06_04.md`. **RFC bring-up now folds into clean DT patches.**

**2026-06-04 ✅ NPU PROBES + CONV RUNS ON STOCK MAINLINE v7.1-rc6.** The whole mainline
bring-up chain is solved and the conv was exercised end-to-end on a clean tree. Result =
the SAME HW compute gate as 6.19 (reproducible), so this is now an RFC with a precise boundary.
(NOTE: items 4 & 5 below were the board-test path; superseded by the DT-only fix above.)

**Mainline bring-up chain (all on LXC ctr100 `/root/rocket-upstream/mainline`, board l4):**
1. **initrd** for UUID root (else VFS panic).
2. **RK3568_PD_NPU power-domain DT child** (else NPU/IOMMU defer -110).
3. **Simon Xue per-device IOMMU ops** (`git am`'d v2 patch) — fixes v1(NPU,32-bit)/v2(rest)
   coexistence that was `rk_iommu_probe:1234` WARN_ON -EINVAL (single global `rk_ops`).
4. **genpd no-op for the "npu" domain** in `pmdomain/rockchip/pm-domains.c` `rockchip_pd_power`
   (early-return) — stops the boot power-off QoS external abort (qos_npu behind gated bus).
5. **Early NoC de-idle in `rocket_register()` BEFORE `platform_driver_register()`**
   (`rocket_rk3568_early_noc_deidle`, drivers/accel/rocket/rocket_drv.c). NEEDED because
   fw_devlink resumes the NPU IOMMU as a runtime-PM *supplier* (→ `rk_iommu_enable_stall`
   reads MMIO) BEFORE rocket_probe; `rk3568_noc_init` in probe is too late. The de-idle needs
   **vdd_npu regulator @1.0V + SCMI PVTPLL @1GHz + PMU power/auto-idle/de-idle** — missing
   vdd_npu was why the first attempt timed out. Success dmesg:
   `rocket: NoC de-idle ret=0 BUS_IDLE_ST=0x1ea` / `[drm] Initialized rocket 0.0.0 for rknn` /
   `rocket fde40000.npu: Rockchip NPU core 0 version: 0` (NO external abort).

**Conv result on 7.1-rc6 (teflon over serial — eth is DOWN on 7.1, no SSH):**
`out_sum=26214400` ×5 = 204800 × 128 EXACTLY = pure zero-point; `rocket: NPU job timed out` ×5.
= the documented HW-internal compute gate (CNA→CMAC→DPU never completes), reproduced verbatim
on mainline. Full stack (mesa libteflon.so → rocket DRM /dev/accel/accel0 → per-device IOMMU →
submit) WORKS; only NPU compute completion is unsolved.

**▶ NEXT SESSION: Track B — finalize the v3 RFC for review (NOT send).** Update cover letter
with the v7.1-rc6 reproducible result + the 5-item dependency chain above; fold PD-child into the
DT patch; keep the clean v3 series free of the board-test scaffolding (items 4 & 5 are NOT
upstream-ready — they belong in the PD/clock layer; disclose as the open "proper NPU bring-up"
item); re-run gates (§5 / PRE_SUBMISSION_CHECKLIST). Board-test scaffolding (genpd no-op +
early-deidle) stays board-only. See `memory/board_7.1_rc6_iommu_coexist_2026_06_02.md`.

**Board state:** on l4 (7.1-rc6), NPU up, **no network** (eth down on 7.1). default=l4.
To restore networked board: reboot → pick **l3** at U-Boot (6.19, SSH works) → set default l3.

(Below: the pre-7.1 convolution analysis, still valid for the conv itself — the gate is the same.)

The reference `rknpu` (vendor) driver runs inference fine on the same board (l1).
The `rocket` driver (l3, 6.19) configures the whole pipeline but **the convolution never
completes** — output stays uniform 128, no completion IRQ, ~540 ms timeout.

As of 2026-06-01 the problem is sharply localized: **PC fetches & broadcasts the
regcmd correctly (cold-boot proven), the configs are correct, sub-units enable — but
CNA's convolution data flow never completes.** The gate is **downstream of config
broadcast**, in the data-movement/completion path, and is **not expressible in the
regcmd** (content fully exhausted).

---

## 2. PROVEN FACTS (do not re-test)

1. **PC broadcasts the regcmd fresh and correctly.** Cold-boot test (2026-06-01):
   CNA config regs go `0` (cold) → correct mesa values after one run. Not stale.
2. **regcmd CONTENT is not the gate.** Byte-exact BSP overlay (values) AND the BSP
   OP_ENABLE scheme (`0x_00c` offsets, BSP values, no CNA enable, interleaved) both
   tested — neither executes. Ping-pong on and off both tested.
3. **PC programming is byte-identical to the working `rknpu` driver** (BASE, AMOUNTS,
   INT_MASK=0x10300, INT_CLEAR, TASK_CON=(0x6<<12)|6, OP_EN 1→0 pulse).
4. **CNA's state `S_STATUS=0x1 / OP_EN=0` is a VALID working state** — it exactly
   matches BSP's working submission #3 (l1 capture 2026-05-30). CNA enable was never
   the bug.
5. **BSP uses NO task descriptor** for this path (`TASKDMA=0`); rocket's descriptor
   model is unnecessary/divergent.
6. **Clocks/resets match BSP:** PVTPLL/SCMI 1 GHz compute clock, srst_a/srst_h (RK3568
   has only these; `state_init`=NULL for RK3568). Not the gate.
7. **CONV_CON4 (0x1018)** must be non-zero historically to "activate", but for THIS
   conv BSP writes `0x1018=0` — the activation it caused was incidental, not correct.
8. **IOMMU `0x11` is NOT idle** (IDLE = bit 3, clear in 0x11). After OP_EN the NPU AXI
   master is **active/non-idle**; cold run shows `ST=0x1` (replay buffer non-empty =
   pending/stalled translations). Prior "IOMMU idle, no AXI" was a misread.
9. **Input/weight BOs contain correct data**; addresses (`FEATURE_DATA_ADDR`, weight
   DCOMP addr, DPU `DST_BASE`) are valid and point at mapped BOs.
10. **CNA config registers are PC-broadcast-only** (reject CPU writes); CONV_CON4
    (0x1018) is the lone CPU-writable exception. No CPU shortcut to configure CNA.
11. **librknnrt is pure ioctl** (no MMIO) → any "invisible HW state" lives in the BSP
    `rknpu.ko` source (which we have), not userspace.
12. **Cache coherency is correct + not the gate** (2026-06-01). Kernel reads BO RAM
    directly: INPUT_BO = `81 81 81…` (real uniform data), WEIGHT_BO = `e9 0a c9 04…`
    (real weights) — both correctly flushed to DRAM. Mesa's prep/fini path is right
    (same path the regcmd uses, which provably works). So input/weight reach RAM fine.
13. **The DPU writes NOTHING** (2026-06-01). OUTPUT_BO RAM = `00…` (zero-alloc, never
    written). `out_sum=26214400` (uniform 128) is just **TFLite's zero_point dequant
    of the unwritten/zero output** — NOT "DPU writes zero_point" (that was a misread).
14. **★ CNA DOES read input + weights; the gate is the COMPUTE→OUTPUT path, NOT
    pre-DMA** (2026-06-01, perf counters — the big reframe). With the NPU data-amount
    counters properly enabled (vendor seq `0x80000101`/`0x00000101` → `0x8010`), a job
    yields `dt_rd≈0xb3` (data read) and `wt_rd≈0x38` (weight read) **non-zero** —
    CNA's CDMA fires and reads input+weights — but `dt_wr=0` (DPU writes nothing). All
    4 downstream sub-units enabled. So the ~2-year "CNA never starts / pipeline
    starved" model is WRONG: the bug is in **MAC-compute → DPU-output-write**, not
    CNA-start. Enabling CMAC (`cmac_open=1`) does not make DPU write.
15. **PMU NoC de-idle is fine** (2026-06-01). `BUS_IDLE_ACK(0x60)`==`BUS_IDLE_ST(0x68)`
    ==`0x1ea` (bit2 clear=active). The `0x60→0x68` poll alignment is correct-to-match-
    vendor but was never the gate.
16. **`bsp_cna` FIXES the CNA read; gate isolated to DPU-WRITE** (2026-06-01). Counter
    unit = **8 bytes**. With `bsp_cna=1`, `wt_rd=0x1900`=51200B = full weights;
    `dt_rd=0x3292`≈full input. (Without it, mesa's 4× line_stride etc. → CNA read only
    ~1.4 KB.) So **CNA now reads the entire input+weights, all 4 units run, but DPU
    still writes 0** (`dt_wr=0`). The bug is the DPU output-write stage alone.
17. **For rocket, mesa's `0x_008` writes are the operative sub-unit enable, NOT
    `0x_00c`** (2026-06-01). `op_en_fix` (NOP 0x_008, write 0x_00c) → all units
    run-status 0, CNA barely reads → HARMFUL, reverted. Corrects the old
    op_en_offset_breakthrough (0x_00c was contaminated). `cmac_open` (append CMAC
    0x2008) is the right way to add CMAC's enable.
18. **★ TRACK A2 — the stall is the COMPUTE stage, not DPU-write** (2026-06-01 cont.,
    full-pipeline `S_STATUS` sweep `rocket_pipe_sweep()`, BEST STATE recipe). Sampled
    every sub-unit's `S_STATUS` (0x_000) at AT_OPEN/AT50US/AT1MS/AT5MS/AT10MS+TIMEOUT:
    **ALL FIVE (CNA 0x1000, CMAC 0x2000, ACCU 0x3000, DPU 0x4000, DPU_RDMA 0x5000)
    sit frozen at `S_STATUS=0x1` ("executer 0 operating") for the entire 540 ms — none
    ever advances.** Data-load phase is provably healthy: `dt_rd≈0x328f` (full input),
    `wt_rd=0x1900` (51200 B = exact full weights), IOMMU returns to **idle 0x19** (reads
    COMPLETED, not stalled/faulted), CBUF_DBG non-zero (`0x1210=0x01ff0000, 0x1234=0x20,
    0x1280=0x78, 0x1284=0x00060006` = CBUF has content). Yet `dt_wr=0`, no IRQ, timeout.
    So **`dt_wr=0` is a SYMPTOM, not the gate** — CMAC/ACCU never compute, so DPU has
    nothing to write. Gate localized to the **CNA(data-loaded)→CBUF→CMAC(compute)**
    hand-off: a producer-consumer deadlock where CBUF is filled but the MAC never drains
    it. (Refines fact 14's "compute→DPU-output" to "the COMPUTE start is the stall".)
    *Caveat: my sweep's `CBUF_CON0 bit13` litmus was wrong — TRM says `data_reuse` is
    bit **12**; rely on perf counters + S_STATUS, not the bit13 print.*
19. **★★ A3 BSP SIDE-BY-SIDE (2026-06-01, our EXACT conv on l1, decisive)** — ported the
    pipeline sweep + perf into BSP `rknpu_job_subcore_commit_pc`, built BSP kernel #68,
    booted l1, ran `conv2d-bsp.rknn`. **BSP `dt_wr=0x6400 = 25600×8 = 204800 B = EXACTLY
    the full output (40×40×128)** — BSP's DPU writes the complete result. Reads match
    rocket almost exactly (BSP `dt_rd=0x32c9`/`wt_rd=0x1900` vs rocket `0x328f`/`0x1900`).
    CBUF_CON0=0x44 on BOTH. **So the load path is identical; the divergence is purely the
    compute→output (now confirmed against ground truth, not inferred).** This also answers
    the old A1 perf question (BSP `dt_wr`≠0) without a separate step.
20. **★★ THE GATE = HW executer-engage (S_POINTER bit 16), NOT submit-config** (2026-06-01,
    A4). Drove rocket to the closest-to-BSP config ever: `bsp_cna=1 cmac_open=1 desc_mode=0`
    (no descriptor → TASKDMA=0, TASKST=0x11000, both BSP-like) + **ping-pong KEPT** (nop=-1
    → S_POINTER write 0xe lands). Result table (all else now matches BSP):
    | | reads | CBUF_CON0 | TASKDMA | TASKST | S_POINTER | S_STATUS | dt_wr |
    |---|---|---|---|---|---|---|---|
    | BSP (works) | full | 0x44 | 0 | 0x11000 | **0x1000e** | **0x0c** | **full 0x6400** |
    | rocket | full | 0x44 | 0 | 0x11000 | **0x0e** | **0x05** | **0** |
    The ENTIRE remaining difference is **S_POINTER bit 16 (0x10000)** — a HW-set bit (CPU
    writes to it are silently dropped) = "executer has latched/engaged the ping-pong group".
    BSP's HW sets it → full output; rocket's HW never sets it → nothing. Everything in the
    command-list + submit path now matches BSP. **The gate is HW-arming the vendor driver
    does at probe/init/submit that rocket omits — NOT expressible in the regcmd.** Ping-pong
    on/off only flips rocket between S_STATUS 0x05 (pp on, S_POINTER 0x0e) and 0x01 (pp off,
    S_POINTER 0); neither engages the executer. desc_mode on/off only flips TASKDMA/TASKST.

---

## 3. RULED OUT (disproven hypotheses — don't repeat)

- **NPU CLOCKS (2026-06-07, definitive A/B).** rocket clk_npu/aclk_npu/pvtpll were
  12MHz vs BSP 200MHz (rocket set only SCMI clock, not CRU CLK_NPU); fixed via DT
  `assigned-clocks <&cru CLK_NPU>=600M` → now byte-identical to BSP (clk_scmi 1GHz,
  clk_npu/aclk 200MHz) → conv STILL uniform-128. Clocks are NOT the gate. (The
  CLK_NPU fix is still a real v4 bug — keep it.) PVTPLL/compute-clock also ruled
  out (1GHz on both). NoC 0x70 auto-idle-disable: clean-genpd dropped it (l5 bit2
  stuck) but 6.19 had it + gated → probably not the gate either.
- regcmd content / values / op-enable offset / op-enable order / compound op-enable
- CNA S_POINTER values (0/0x1/0xe/0x2/0x5/0x10/0x20/0x30), CBUF_CON0 (0x17/0x44)
- CONV_CON1=0xa000 (**WEDGES the NPU — needs reboot to recover; never set it**)
- ping-pong on vs off (off lands configs + "4/5"; on stalls PC at TASKST=0x9000)
- task descriptor model (BSP doesn't use it); TASK_DMA_BASE variants
- BSP slave-mode `PC_DATA_ADDR=0x1` pre-write (`pc_slave_pre`)
- task_number (controls only PC's completion counter, not execution)
- dma_mask 32 vs 40 (32 correct, already set); bw_priority; MYSTERY/CMAC 0x2xxx init
- HIGH vs LOW IOVA; AUTO_GATING (fixed long ago); DT clock-names ordering
- The old "4/5 sub-units running" — was just the **written value persisting in a
  status reg (0x_008)**, never real computation.
- **Cache coherency** (2026-06-01): input/weight provably in RAM; mesa prep/fini
  correct; not the gate. (PLAN step 3 — done.)
- **TASK_CON task_pp_en (bit12)** (2026-06-02): tested SET with TASKDMA=0 (true BSP combo)
  → executer still doesn't engage; only flips TASKST 0x11000↔0x21000. Not the gate.
- **task descriptor / TASKDMA** (desc_mode): on/off only flips TASKDMA/TASKST; not the gate.
- **IOMMU per-job detach/attach**: only fires job 0 (already attach-once like BSP); not it.
- **PC submit register writes** (BASE/AMOUNTS/INT_MASK/INT_CLEAR/TASK_CON/TASKDMA/OP_EN):
  all match BSP (2026-06-02). BSP BASE=0 after OP_EN is just PC consuming it (completion).
- **Byte-exact BSP regcmd REPLAY** (2026-06-02): rocket ran BSP's exact 133-entry list,
  config verified landed in CNA MMIO, BOTH ping-pong forks → no execution. Content closed.
- **Vendor full reset port** (`full_reset`, 2026-06-02): ported `rknpu_soft_reset` (msleep +
  CRU assert/deassert + PVTPLL re-init + NoC de-idle + IOMMU re-establish). Tested BOTH
  forks — full_reset=1 (on resume) AND full_reset=2 (per-job, FULL_RESET confirmed firing).
  Identical to baseline: full reads, dt_wr=0, executer hangs. Reset/power layer closed.
  (Vendor's NORMAL path is `rknpu_power_on`, already mirrored by rocket resume; soft_reset
  is only the ACT_RESET ioctl / timeout path — not how BSP normally runs.)
- **mem/sram regulator**: RK3568 `sram-supply = <&vdd_npu>` = SAME rail as npu-supply
  (already powered, always-on + OPP). Vendor's separate `mem` regulator is the same physical
  rail. Not an unpowered SRAM. Ruled out.
- **Simultaneous reset release** (`reset_sim`, 2026-06-02): direct CRU SOFTRST_CON2 write to
  release srst_a+srst_h at once (TRM 9.5.2), both forks (resume + per-job) → dt_wr=0, no change.
  The last documented HW nuance — closed.
- **TRM-documented pipeline-start model** (2026-06-02 read): no missing command-path register;
  PC-mode enable = pc_op_enable (matched); GLOBAL_OP_EN is direct-mode only; CMAC start = config
  only, no separate trigger. Gate is HW-internal.

---

## 4. Leading Hypothesis (2026-06-02 EOD — content RULED OUT; gate is HW-init/driver state)

**The gate is the driver's hardware-init/power/reset/IOMMU state — NOT the command list.**
DEFINITIVE PROOF (facts 21–22): rocket ran BSP's BYTE-EXACT 133-entry regcmd (config verified
landed in CNA MMIO: CONV_CON2=0x70, WEIGHT_SIZE=0xc800/0x5050080, DATA_SIZE all BSP; enables
CMAC 0x200c=1/ACCU 0x300c=0/DPU 0x400c=0x108/DPU_RDMA 0x500c=0x27; AMOUNTS=0x88; both ping-pong
forks) → STILL dt_wr=0, no execution. BSP runs the SAME list → full 204800B output. Same
instructions + same config-in-registers + same IOVAs → opposite result. So content (values,
structure, order, ping-pong, enable scheme) is exhaustively ruled out. The vendor `rknpu`
driver establishes some HW state at probe/power/reset/IOMMU that `rocket` doesn't, which is
what actually lets the NPU execute. Candidates (the only untested layer left):
  (a) **rknpu_soft_reset full sequence** — msleep(100), assert/deassert ALL resets, PVTPLL
      re-init, NoC re-idle, IOMMU detach/reattach + rk_iommu_trigger_enable. rocket has minimal
      srst_a/srst_h. Even if clocks/IOMMU "look" matched, the RESET ORDER/completeness may arm
      the executer.
  (b) **probe-time power/clock/NoC sequencing** in rknpu_drv.c that rocket's rocket_drv.c omits.
  (c) A genuinely HW-internal arm needing TRM/vendor input → RFC.
Next: deep diff rknpu_drv.c probe + rknpu_reset.c vs rocket_drv.c/rocket_core.c for init-time
register writes; test the vendor reset sequence on rocket. If no observable diff → RFC.
*(Below: the prior compute-stage hypothesis that led here — now superseded; content ruled out.)*

### (prior) executer starts but hangs at compute — weight-BO packing
**The executer STARTS but HANGS at compute; leading suspect = mesa weight-BO packing.**
TRM S_STATUS decode: rocket `0x05`=status_0=1 (executer operating) stuck; BSP `0x0c`=
status_0=0 (idle=done). Rocket reads full input+weights (perf counters) then the MAC
hangs and never produces output (dt_wr=0); BSP completes. Every PC/submit register now
matches BSP (BASE, AMOUNTS, INT_MASK, INT_CLEAR, TASK_CON incl. task_pp bit12, TASKDMA,
OP_EN, ping-pong) and CBUF register values (bsp_cna) — so the hang is in the DATA the MAC
consumes or a HW-arm, not a register. **Top suspect: mesa's weight tensor PACKING in the
weights BO** (`rkt_coefs.c`, RK3588-derived `[K_group][C_group][W][H][K_atom][C_atom]`
32×32 tiles, `CBUF_BANKS=12` vs RK3568's 8). `bsp_cna` fixes register VALUES but NOT BO
contents; the RKNN-packed `conv2d-bsp.rknn` runs on BSP. This is ALSO the Track-B mesa fix.
Decisive alternative: byte-exact REPLAY of BSP's 133-entry regcmd (IOVAs swapped) — settles
content-vs-HW once and for all (heavy: needs IOVA remap).
*(Below: the A3/A4 facts that led here.)*

### (prior) gate = HW executer-engage S_POINTER bit 16
A3/A4 (facts 19–20) collapsed the BSP↔rocket difference to a single HW-set bit:
BSP reaches `S_POINTER=0x1000e` (bit16 set = executer latched the ping-pong group) →
`S_STATUS=0x0c` → `dt_wr`=full output; rocket reaches `S_POINTER=0x0e` (bit16 never set)
→ `S_STATUS=0x05` → `dt_wr=0`. Everything else now matches BSP (reads full, CBUF_CON0=
0x44, TASKDMA=0, TASKST=0x11000, config values via bsp_cna, ping-pong write 0xe). So the
executer never engages on rocket despite identical command-list + submit config. The
missing arming lives in the **vendor `rknpu` driver code** rocket doesn't replicate:
  (a) **Driver init register writes** (`rknpu_drv.c` probe + `rknpu_reset.c` soft_reset):
      NoC (0xfe18xxxx) / CRU / PMU / IOMMU sequencing rocket omits. *(top lead — we have
      both sources in `srcdiff/`; no-reboot source diff.)*
  (b) **IOMMU domain handling** — BSP keeps the domain attached; rocket does per-job
      detach/attach. Could leave the NPU AXI/executer in a different armed state.
  (c) **A submit-time HW poke** outside the PC register block (not pc_slave_pre, that's
      disproven) — e.g. a sub-unit S_POINTER/clk-gate write, or `rknpu_soft_reset` steps.
Because bit16 is HW-set (CPU writes dropped), we can't force it — must find what makes
the HW set it. *(Superseded: "compute/CBUF-bank is the gate" — A3 shows BSP uses the same
CBUF_CON0=0x44 and same reads, so CBUF config is NOT the diff; the executer-engage is.)*

---

## 5. NEXT STEPS — ✅ RFC v3 SENT + first maintainer reply (Tomeu) 2026-06-04.

> 🎉 **Tomeu Vizoso (rocket maintainer) replied (2026-06-04) — positive + collaborative.** Details
> in `memory/tomeu_v3_feedback_2026_06_04.md`. Three concrete next moves (all need USER go + creds):
> 1. AUTO_GATING — **RE-REVERSED 2026-06-05 by Chaoyi Chen (Rockchip).** We had dropped it (our `0x2`
>    was a wrong guess that looked harmful). Chaoyi gave the REAL fix: `auto_gate |= BIT(31)` (RMW,
>    BEFORE enable_paging, keeps reset 0x3). **Patch 6 is KEPT — replace with his bit-31 version**,
>    test on board, add `Suggested-by: Chaoyi Chen`. v4 = 9 patches again. He also says the NPU MMU is
>    **v2** (not v1), DTE cap 32-bit — reshapes the IOMMU dependency (our v1-force was a workaround for
>    the >4GB DTE; clean path is v2 + a 32-bit DTE cap, to settle with Chaoyi + Simon Xue). Draft
>    replies (NOT sent): `upstream-prep/chaoyi-replies-draft.txt`. Memory:
>    `memory/chaoyi_rockchip_feedback_2026_06_05.md`. (The 2026-06-04 "dropped, do not submit" note is
>    obsolete; `autogating-research.md` was right that *our 0x2* was wrong, but a correct fix exists.)
> 2. **Open a mesa GitLab Draft MR** (gitlab.freedesktop.org/mesa/mesa) with the RK3568 Teflon work
>    (mesa-patches/) to collaborate with Tomeu (he has no rk3568 HW but will guide).
> 3. **Ping Tomeu on #ml-mainline (OFTC IRC)** for compute-gate tricks — he hit the same wall on
>    RK3588 enablement. This is the highest-value lead on the 2-yr compute gate.
> His debug method (= our byte-exact approach): simplest workload → diff ALL HW payloads → then ALL
> kernel register writes. Only patch 6 + mesa replied so far; driver/binding/DT + bot findings pending.



> 📨 **SENT (2026-06-04):** `[RFC PATCH v3 0/9] accel: rocket: Add RK3568 NPU support` via
> git send-email (LXC ctr100, Gmail SMTP) — all 10 emails accepted (SMTP 250). To: Tomeu, Oded,
> Heiko, Rob, Krzysztof, Conor, Joerg, Will. Cc: Robin Murphy + dri-devel, linux-rockchip,
> devicetree, linux-arm-kernel, iommu, linux-kernel. See `memory/upstream_v3_rfc_sent_2026_06_04.md`.
>
> **▶ IMMEDIATE TODO:** (1) ✅ Gmail app password revoked (2026-06-04). (2) do NOT push to any git
> repo until maintainers okay (user's standing rule) — the RFC is the review request, not the merge.
> (3) watch lore for feedback (search "Add RK3568 NPU support" on linux-rockchip/dri-devel) and work
> the replies. (4) ✅ **mesa report SENT to mesa-dev (cc Tomeu) 2026-06-04** (SMTP 250) — source at
> `upstream-prep/mesa-rk3568-email.txt`. Revoke that app password too. Watch mesa-dev/Tomeu reply.
> Likely RFC review points: AUTO_GATING applied unconditionally (gate to v1 ops?), vdd_npu always-on
> policy, the Simon per-device-ops dependency, and the unsolved compute gate.
>
> **▶ Sashiko AI bot reviewed v3** (2026-06-04) — found real bugs; full v4 to-do in
> `memory/sashiko_bot_review_v3_2026_06_04.md`.
> **APPLIED + verified on branch rocket-rk3568-v3-genpd (kernel #12, NPU still probes):**
>   - 328571b: rk3568_noc_init — SCMI clock by name (was clks[2]=aclk) + DROP driver PMU de-idle
>     (genpd owns it). #4 was the risky one; proven safe (genpd-alone de-idle works).
>   - 59bdf2f: clear attached_domain on reset (else IOMMU unconfigured after hard reset → DMA fault).
>     Runtime-confirm via teflon timeout path optional/pending.
> **STILL TO DO for v4:** AUTO_GATING (gate to v1 ops + HW re-check 0x1-vs-0x2 + RMW — needs board),
> noc_init error unwind, binding pmu-required-for-rk3568, cores[] OOB bound-check, fini ordering.
> Then **regenerate the send-series** (rocket-rk3568-v3-send is still the AS-SENT v3; v4 = fold these
> into patches 3/5/6/7) — only after HUMAN maintainer replies + user review.
>
> **Longer-term (future sessions), in priority order:**
> a. Respond to RFC feedback (human + the Sashiko bot); iterate to v4 (fold the bot's valid fixes +
>    human feedback; rebase onto current -next per GATE 0). Don't send v4 until user review.
> b. **The compute gate is the real merge blocker** (Heiko's "works first" bar): NPU MAC/output
>    stage never completes (uniform-128). Content ruled out (byte-exact vendor regcmd replay also
>    fails) → it's HW-init/driver state. Candidate next attacks: full rknpu_soft_reset port;
>    per-stage BSP-vs-rocket compare with the perf/DMA counters; the first-attach idle-MMU rk_iommu
>    residual may share root. See memory: executer_hangs_weight_layout, a3_bsp_executer_engage.
> c. mesa per-SoC config rework (rkt_regcmd.c / rkt_ml.h) for RK3568 once the HW gate is understood.

### (history) RFC send-ready prep

> ✅ **Send-ready 9-patch RFC v3 generated & staged (2026-06-04).** Clean branch on LXC ctr100:
> `rocket-rk3568-v3-send` (= v7.1-rc6 + Simon's e9f41b7 dep base `52c800fd` + our 9). Verified:
> tree IDENTICAL to `rocket-rk3568-v3-genpd` (faithful reorg); checkpatch --strict **0/0/0 on all
> 9**; dt_binding_check EXIT 0; every commit has Signed-off-by. Patch files on the workstation:
> `upstream-prep/v3-patches-final/` (v3-0000 cover + v3-0001..0009). Cover = `upstream-prep/
> v3-cover-letter.txt` (folded into 0000). The 9 patches:
>   1 rocket: per-SoC data  2 derive DMA/cores  3 RK3568 SoC  4 reset-before-detach
>   5 attach-once  6 iommu AUTO_GATING  7 binding  8 dts NPU+IOMMU(+PD,no pm_qos)  9 dts enable(+vdd always-on)
> Dependency cited (not in series): Simon Xue per-device IOMMU ops [1].
> Recipients (get_maintainer): To = Tomeu, Oded, Heiko, Rob, Krzysztof, Conor, Joerg, Will;
> Cc = Robin Murphy + dri-devel, linux-rockchip, devicetree, linux-arm-kernel, iommu, linux-kernel.
>
> **NOT SENT** (user reviews; maintainers must okay before any repo push). Remaining to actually send
> (user-authorized): git send-email is NOT on the container — send from a machine with SMTP, or
> install git-email. Optional pre-send polish: GATE 0 rebase onto current drm-misc-next/linux-next;
> GATE 8 b4/self-send dry-run. The RFC base is v7.1-rc6+Simon (a release point; fine for RFC).

> ✅ **RFC v3 finalized for review (2026-06-04).** Branch `rocket-rk3568-v3-genpd` (LXC ctr100)
> = v3 + 3 session fixes (53f8d03 DT bring-up, 81c8463 reset-WARN, dac5884 attach-once). Cover
> letter rewritten: `upstream-prep/v3-cover-letter.txt` (9-patch plan, honest v7.1-rc6 status,
> Simon per-device-ops dependency + first-attach residual disclosed). Gates: checkpatch --strict
> CLEAN on diffs (only SoB + one 75-char msg line); dt_binding_check EXIT 0; series builds.
> **NOTHING SENT — user reviews first; maintainers must okay before any push.**
>
> **Pre-send mechanical steps (after user review):**
> (a) git consolidation: fold PD-child (08faee4) + genpd-bringup (53f8d03) into the DT node/enable
>     patches; reword the "board-test" labels (08faee4, 043be7a AUTO_GATING are real); EXCLUDE
>     Simon's e9f41b7 from our series (cite as dependency). Target = the 9-patch list in the cover.
> (b) add Signed-off-by to every commit; fix the 1 over-length commit-msg line (81c8463).
> (c) GATE 0: rebase onto current drm-misc-next/linux-next (series is on v7.1-rc6 tag now).
> (d) GATE 8 self-send + b4 apply; GATE 9 get_maintainer for recipients.

### (superseded) prior START HERE: finalize the v3 RFC for review

> ✅ **DONE 2026-06-04:** Simon's per-device IOMMU ops applied; NPU now probes on stock 7.1-rc6;
> conv ran via teflon (serial) → `out_sum=26214400` = uniform zero-point, `NPU job timed out`
> = the same HW compute gate, reproduced on mainline. Full bring-up chain documented in §1 and
> `board_7.1_rc6_iommu_coexist_2026_06_02.md`. The probe/IOMMU step-by-step below is COMPLETE.
>
> ▶▶ **NEXT SESSION — START HERE: Track B, finalize the v3 RFC for review (do NOT send;**
> **user reviews first, maintainers must okay before anything is pushed).** Concretely:
> (a) cover letter: add the v7.1-rc6 reproducible result + the 5-item bring-up dependency chain
>     (§1) + the open "proper NPU bring-up belongs in PD/clock layer" item;
> (b) fold the RK3568_PD_NPU power-domain DT child into DT patch 5 (currently uncommitted/working
>     tree on the LXC v3 branch);
> (c) keep the clean v3 series FREE of board-test scaffolding (genpd no-op + early NoC de-idle =
>     board-only, items 4 & 5 — NOT upstream-ready; disclose, don't ship);
> (d) re-run gates (PRE_SUBMISSION_CHECKLIST). See "Track B" subsection below.
> Board recovery for next session: reboot → pick l3 at U-Boot (6.19, SSH) → set default l3.

### (DONE 2026-06-04) Step-by-step that got the NPU probing on 7.1-rc6
1. **Fetch Simon's patch** (Anubis blocks the HTML — use the LXC curl-mbox trick that worked
   for the IOMMU thread). Base msg-id `20260310105303.128859-1-xxm@rock-chips.com` — but CHECK
   lore for a newer version (it's from Mar 2026; there may be a v2/v3 that's the one to use):
   `ssh root@<TAILSCALE-IP> "curl -sL -A 'mutt/2.0' 'https://lore.kernel.org/linux-rockchip/20260310105303.128859-1-xxm@rock-chips.com/t.mbox.gz' -o /tmp/simon.mbox.gz"`
   then gunzip + extract the patch(es) (no `b4` on the LXC; use `git am` on the mbox, or save
   the diff). Read the thread to confirm it's the per-device-ops approach and the latest rev.
2. **Apply on the LXC** `rocket-rk3568-v3` branch (currently on 7.1-rc6, tag `v3-rebased-7.1-rc6`
   + AUTO_GATING commit `043be7a` + an UNCOMMITTED dtsi edit adding the NPU power-domain child).
   First COMMIT the PD-child dtsi edit (it's a working-tree change; see step 5 to fold it into
   patch 5 later). Then `git am`/apply Simon's patch.
   **CONFLICT TO EXPECT:** our AUTO_GATING fix (`043be7a`, in `rk_iommu_enable` using the global
   `rk_ops`/`rk_iommu_write`) WILL clash with Simon's per-device-ops restructure (it removes the
   global `rk_ops`). Re-apply the AUTO_GATING write in Simon's new per-device structure, on the
   v1-ops path only (RK_MMU_AUTO_GATING=0x2 after enable_paging).
3. **Rebuild + redeploy** (recipe proven this session, LXC→board direct rsync, passwordless):
   ```
   B2=root@<TAILSCALE-IP> ; KREL=$(ssh $B2 'cd /root/rocket-upstream/mainline && make ARCH=arm64 kernelrelease')
   ssh $B2 'cd /root/rocket-upstream/mainline && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j32 Image modules rockchip/rk3568-rock-3b.dtb'
   ssh $B2 "cd /root/rocket-upstream/mainline && make ARCH=arm64 INSTALL_MOD_PATH=/tmp/mod71 modules_install"
   # LXC->board: rsync modules + Image-7.1-rocket + dtb (use --rsync-path='sudo rsync')
   ```
   If only rockchip-iommu.c changed (likely), Image rebuild is incremental + modules unchanged →
   can skip the 115M module rsync (just push Image-7.1-rocket; dtb already has PD-child).
4. **Boot l4, verify probe:** `sudo sed -i 's/^default l3/default l4/' /boot/extlinux/extlinux.conf; sudo reboot`,
   poll (~36s). Check: `rk_iommu fde4b000` probes (NO -EINVAL/-110), genpd summary shows `npu`,
   `rocket fde40000.npu` probes, `/dev/accel/accel0` exists, "Rockchip NPU core 0 version".
5. **If NPU probes → TEST THE CONV** (the real goal): `python3 ~/npu-debug/teflon_test.py`.
   teflon/mesa is kernel-version-independent (uses /dev/accel ioctls) so the board's existing
   mesa works. Expected: out_sum likely still 26214400 (the mesa RK3588-config / HW compute
   gate from §4) — BUT now reproducible on clean mainline, which is what the RFC needs. Re-run
   the rocket debug params (bsp_cna etc.) on 7.1-rc6 if useful — note rocket is built-in (=y)
   on this config, so module params need `rocket.<param>=` on the kernel cmdline OR rebuild
   rocket as =m. (On 7.1 the instrumented srcdiff/rocket_job.c is NOT applied — only clean v3.
   To re-run the param experiments on 7.1, port the instrumentation OR build rocket=m.)
6. **Outcome → decide:** NPU works on mainline → merge-candidate path; still gated → the RFC
   (Track B) is bulletproof AND now has the full dependency chain (incl. IOMMU per-device-ops
   as the hard dep) documented.

### Track B — finalize the v3 RFC (parallel / after)
v3 is built + gated on both 6.19-rc5 and 7.1-rc6 (tags `v3-validated-6.19-rc5`,
`v3-rebased-7.1-rc6`; `upstream-prep/v3-patches{,-7.1-rc6}/` + `v3-cover-letter.txt`). To
finalize: FOLD the NPU power-domain child into DT patch 5 (found missing this session); add the
dependency chain to the cover letter (needs: PD-child DT, AUTO_GATING IOMMU fix, and Simon's
per-device-ops IOMMU patch as a hard prerequisite); strip debug scaffolding; `dt_binding_check`
+ checkpatch + get_maintainer again; send as RFC (Heiko's gate — inference not yet proven).
The IOMMU per-device-ops dependency reframes the RFC: on a full mainline boot the NPU can't even
probe without it, so "doesn't execute" is downstream of that.

### (superseded) prior START HERE — draft the v3 RFC / byte-exact replay (DONE)

### (superseded) prior START HERE — byte-exact replay (DONE, content ruled out)
> **full byte-exact BSP regcmd REPLAY.** All register VALUES
> match BSP (bsp_cna); weight K-atomic=32 is correct for int8; DCOMP matches BSP. The
> remaining diffs are STRUCTURAL (CMAC S_POINTER 0x2004=0xe + 0x200c enable that mesa omits;
> BSP's 0x_00c enable scheme vs mesa's 0x_008; entry ORDER/position — which matters under
> ping-pong). Piecemeal injection can't reproduce BSP's ordering (cmac_bsp append was
> inconclusive — CMAC S_STATUS moved 0x05→0x00 but 0x200c landed in the inactive group).
> The replay reproduces structure+order+CMAC+enable-scheme AT ONCE. DONE/ruled out: A2,
> A3/A4 (BSP dt_wr=full output), task_pp_en, descriptor, IOMMU-attach, ALL register values,
> DCOMP, weight K-atomic, every PC submit write, cmac append.

1. **[DECISIVE] Full byte-exact BSP regcmd REPLAY.** Overwrite rocket's regcmd BO (in the
   kmap patch block) with BSP's exact 132 entries from `rknn-convert/bsp_conv2d_regcmd.txt`,
   substituting ONLY the 4 IOVA fields with rocket's BO addresses: 0x1070 (feature/input),
   0x1110 (DCOMP weight addr), 0x4020 (DPU DST/output), 0x5020 (DPU_RDMA src). Set
   regcmd_count + AMOUNTS to match. Keep ping-pong. Litmus: S_POINTER bit16 sets, S_STATUS
   →0x0c, dt_wr>0, out_sum leaves 26214400. Outcome interpretation:
   - WORKS ⇒ gate was regcmd STRUCTURE/ORDER (mesa rkt_regcmd.c emits wrong structure for
     RK3568) → port the structure fix to mesa (Track B).
   - FAILS ⇒ gate is HW-state/arming outside the command list entirely → step 3 / RFC.
   Implementation: parse the 132 (off,val,routing) lines into a kernel const array; need to
   know which rocket BO IOVA maps to each of the 4 address slots (from rocket's regcmd
   weight/feature/dst entries already dumped at AT10MS).
2. **[supporting] Get rocket's 4 BO IOVAs** for the address substitution (input/weight/
   output/dpu_rdma) from the live AT10MS dumps (FEATURE_DATA_ADDR, DCOMP_ADDR0, DPU DST_BASE,
   DPU_RDMA src) — already logged; just collect.
3. **[fallback] RFC** with the precise localization (facts 18–21) per §5A/end-game.
~~weight BO byte-layout capture~~ deprioritized (K-atomic=32 correct for int8; DCOMP matches).
2. **[no reboot] IOMMU domain handling.** BSP keeps the domain attached (one attach at
   probe); rocket detaches/attaches per job. Test: skip rocket's per-job detach/attach
   (keep attached) and see if the executer engages. (Pairs with step 1.)
3. **[reboot, if 1–2 dead-end] BSP submit-trace.** Instrument BSP `rknpu_job.c` to log
   EVERY `REG_WRITE` (offset+value) during a `conv2d-bsp.rknn` submit; diff the full write
   sequence against rocket's. Captures any poke not visible in static source reading.
4. **[fallback] RFC** with the precise localization (facts 18–20) per §5A/end-game.
~~Coherency~~ / ~~stalled-read~~ / ~~CBUF-bank compute gate~~ — all disproven (see §3).
5. If 1–3 dead-end: package as RFC (driver+DT + precise localization: content/coherency
   not the gate, CNA reads, DPU-write stage is the gate) per end game.

## 5A. TRACK A ROADMAP — make inference produce correct output (the merge blocker)

Maintainers will NOT merge until inference works (no partial code —
`memory/maintainer_feedback_v2_2026_05_29.md`). So solving the DPU-output-write gate
is the prerequisite for the whole upstream push. Pipeline:
`CNA (feature+weights → CBUF) → CMAC (MAC) → ACCU (accumulate) → DPU (bias/act/quant →
write)`, with `DPU_RDMA` feeding DPU bias/residual. Perf counters prove data flows IN
(CNA) but not OUT (DPU `dt_wr=0`). Track A localizes the stalled stage, then fixes it.

**A1 — Confirm the gate (BSP perf compare).** = §5 step 1 (START HERE). Capture BSP
`dt_wr/dt_rd/wt_rd` for `conv2d-bsp.rknn` on l1. Expect BSP `dt_wr`≠0 ⇒ DPU-write is
the unique gate + read parity confirmed. *(reboot)*

**A2 — Localize the stall within CMAC→ACCU→DPU.** *(no reboot — add readbacks to the
existing AT50US..AT10MS blocks in rocket_job.c)* Sample EVERY sub-unit S_STATUS over
time — CMAC `0x2000`, ACCU `0x3000`, DPU `0x4000`, DPU_RDMA `0x5000` — plus CBUF status
(`CBUF_CON0` bit-13 DATA_REUSE = HW "real-exec" litmus) and whether DPU_RDMA actually
reads (bias). Until now ONLY CNA was instrumented; this full-pipeline sweep is the key
move. Find the FIRST stage that doesn't advance:
  | Observation                     | Stall          | Likely cause                          |
  |---------------------------------|----------------|---------------------------------------|
  | CBUF bit-13 never sets          | CNA→CBUF→CMAC  | CBUF bank cfg (RK3568 8 vs RK3588 12)  |
  | CMAC S_STATUS idle              | MAC not fed    | weight format / DCOMP / CBUF           |
  | DPU_RDMA doesn't read           | DPU bias starved| DPU_RDMA config/trigger               |
  | DPU "running" but dt_wr=0       | DPU output-DMA | write-kickoff / DST-valid / flying-mode|
  Run under the BEST STATE recipe (`clean_run=1 bsp_cna=1 cmac_open=1 nop=0..4`).

**A3 — BSP side-by-side for the stalled stage.** *(reboot)* Boot l1, capture BSP's same
per-stage probes for `conv2d-bsp.rknn`, diff ONLY the stalled stage's regs/state vs
rocket. The ground-truth `.rknn` isolates exactly what BSP does at that one stage.

**A4 — Targeted fix + verify.** Fix the identified stage (CBUF banks / DCOMP weight
format / DPU output-DMA trigger / DPU_RDMA bias). Litmus ladder: `dt_wr`≠0 → CBUF
bit-13 sets → IRQ fires → `out_sum` leaves 26214400 → matches `23,252,430`.

**A5 — Escalate if HW-internal.** If A2–A4 find no observable BSP/rocket difference at
the stalled stage → TRM deep-read of DPU/CMAC start semantics (`docs/npu-trm-decoded.md`
+ PDFs), vendor consult, or RFC. Bounded attempts, then decide.

Strongest priors: **CBUF bank allocation** (long-suspected, never confirmed under the
corrected-config regime) and the **DPU output-DMA trigger**.

## 5B. TRACK B — upstream readiness (only after Track A succeeds)

- Fold the real fixes into **mesa userspace** (`rkt_regcmd.c`): the `bsp_cna` config
  corrections (4× line_stride etc.) are mesa RK3568 bugs; kernel params are debug
  scaffolding, not the fix.
- **Strip rocket_job.c clean** (2451 lines of experiment scaffolding → minimal upstream
  driver).
- Maintainer feedback: split patches, derive DMA/cores from `match_data`, bindings
  describe HW + `dt_binding_check` passes (the gate v2 failed), terse cover letter, v3
  numbering, `get_maintainer.pl` recipients. See `upstream-prep/PRE_SUBMISSION_CHECKLIST.md`.
- Retest on latest STABLE mainline (not rc5), then send v3.

---

## 6. Reproduction / Tooling (the "usual commands")

**Board:** Radxa ROCK 3B, RK3568, 8 GB. user/pass `radxa`/`radxa`. NOPASSWD sudo.
- Reach over Tailscale **`<BOARD-TAILSCALE-IP>`** (office board; home access via TS).
  **Gotcha:** path had an MTU blackhole — large pkts dropped, SSH froze at KEX. Fix on
  *this host*: `sudo ip link set tailscale0 mtu 1000` (≤1000-byte payload survives).
- Boot entries: **l3** = rocket (`Image-6.19-rocket`); **l1** = BSP rknpu (`Image-6.19`).
  Default boots l3. `sudo reboot` returns in ~20–60 s. To switch entries, edit extlinux
  default. Board is REMOTE — a reboot that hangs needs physical access.

**Build + reload rocket module (no full kernel build needed):**
```bash
B=radxa@<BOARD-TAILSCALE-IP>
scp rocket_job.c $B:npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c
ssh $B 'cd ~/npu-port/linux-mainline && make ARCH=arm64 CROSS_COMPILE= -j4 M=drivers/accel/rocket'   # ~30s
ssh $B 'sudo rmmod rocket; sudo insmod ~/npu-port/linux-mainline/drivers/accel/rocket/rocket.ko <params>'
```

**Run inference + capture:**
```bash
ssh $B 'sudo dmesg -C; python3 ~/npu-debug/teflon_test.py'   # 5 runs, prints out_sum
ssh $B 'sudo dmesg | grep -E "AT10MS|OPEN_8|CNADUMP|IOMMU|out_sum|TASKST"'
```

**rocket module params** (rocket_job.c; all 0644-writable via `/sys/module/rocket/parameters/`):
`clean_run` (1 = native mesa regcmd, **gates OFF** the contaminated OP_EN_OFFSET_FIX —
keep =1), `nop_start`/`nop_end` (NOP regcmd slot range; `0..4` = ping-pong off, `-1` =
keep), `cmac_open`, `desc_mode`, `bsp_cna` (BSP config-value overlay), `op_en_fix`
(2026-06-01, clean BSP `0x_00c` enables — **HARMFUL, leave 0**), `pc_slave_pre`
(2026-06-01, no effect). **Avoid `conv_con1` (0xa000 wedges).**
**BEST STATE recipe (2026-06-01):** `clean_run=1 bsp_cna=1 cmac_open=1 nop=0..4`
(op_en_fix=0 pc_slave_pre=0) → CNA reads full input+weights, 4 units run, but DPU
writes 0 (`dt_wr=0`), out=128. This is the current frontier.

**Perf-counter probe (2026-06-01, live in rocket_job.c):** enable+clear at PRE_OP
(`0x80000101` then `0x00000101` → `0x8010`), read `PERF_POST` at AT10MS:
`dt_wr`(0x8034)=DPU bytes written, `dt_rd`(0x8038)=CNA data read, `wt_rd`(0x803c)=CNA
weight read — **all in 8-byte units**. This is the decisive "did data move" probe;
must use the enable seq or counters read 0 (disabled).

**BSP for our exact conv:** `rknn-convert/` has `conv2d-bsp.rknn` + the docker-py3.10
rknn-toolkit2 1.5.2 pipeline + `run_rknn.c` + `bsp_conv2d_regcmd.txt` (133 entries,
ground truth) + `diff_regcmd.py`. Reusable to capture BSP for any conv shape.

**Cold-boot clean test (resolves stale readbacks):** `sudo reboot`, wait, then ONE
teflon run; the first run's AT10MS readback is on a register-cleared NPU.

**Source trees on board:** `~/npu-port/linux-mainline/drivers/{rknpu,accel/rocket}/`.
Local mirror of both job/init files: `srcdiff/`. TRM extract: `docs/npu-trm-decoded.md`.

---

## 7. Session Log (append newest at top)

### 2026-06-07 — Chaoyi engaged; clocks RULED OUT; NoC gap found; WiFi/l5 rebuilt
- **Chaoyi Chen (Rockchip) replied to RFC v3** (2026-06-05): NPU MMU is IOMMU **v2**
  (not v1), DTE cap 32-bit; AUTO_GATING real fix = `auto_gate|=BIT(31)` RMW (UN-drops
  patch 6). Both reply drafts FINALIZED + send-ready (`upstream-prep/chaoyi-replies-
  draft.txt`); TF-A confirmed = upstream ATF. See [[chaoyi-rockchip-feedback-2026-06-05]].
- **Built + tested on board (LXC was down → built on nvme, then LXC):** l5 =
  v7.1-rc6 + Simon v2 + v3(1-5,7-9) + v4 fixes + Chaoyi BIT(31) + genpd NPU-always-on.
  Chaoyi's BIT(31): clean boot, NPU probes, no IOMMU regression (l4 0x3 A/B == l5).
- **COMPUTE GATE — clocks RULED OUT (definitive):** rocket NPU clk_npu/aclk_npu were
  at **12MHz** (vs BSP 200MHz) — rocket set only the SCMI clock, never CRU CLK_NPU.
  Fixed (DT `assigned-clocks <&cru CLK_NPU>=600M`) → clocks now **byte-identical to
  BSP** → conv **STILL uniform-128**. So clocks aren't the gate (but the CLK_NPU fix
  is a real v4 bug). NoC de-idle gap also found (clean-genpd dropped BSP's 0x70 NPU-
  NoC-auto-idle-disable → bit2 stuck) but 6.19 had it + still gated → probably not
  the gate. Gate = deeper HW-init/executer-engage. Expert Q drafted:
  `upstream-prep/compute-gate-expert-question.md`. See [[compute-gate-pvtpll-hypothesis-2026-06-07]].
- **WiFi on l5 fixed** (agent, via LXC full-modules build): l5 = 7.1.0-rc6-chaoyi-00011,
  wlp1s0 + eth MAC + NPU all good. **v4 spec written** (`upstream-prep/v4-changeset-spec.md`).
- Board: on l5, **default l3** (safe). LXC + board reachable.

### 2026-06-02 (cont.) — ★★ BOARD on 7.1-rc6: boots; NPU blocked by IOMMU v1/v2 coexistence
- Built full 7.1-rc6 kernel (v3 series + AUTO_GATING port; GFP_DMA32 already upstream in v1
  ops) on the LXC, deployed Image+modules(115M)+dtb to the board as extlinux **l4** (non-
  destructive; l0-l3 intact). kernelrelease 7.1.0-rc6-00007-g043be7a551c4.
- **7.1-rc6 + v3 BOOTS** (after fixing two issues): (1) l4 needed an **initrd** (UUID root
  resolution panics without one — generated `update-initramfs -c -k`); (2) NPU+IOMMU
  deferred -110 because the **RK3568_PD_NPU power-domain child was missing from the DT**
  power-controller (in the driver table but not declared) — added a `power-domain@
  RK3568_PD_NPU` child (dtb-only rebuild).
- **★ THE BLOCKER (decisive): NPU IOMMU v1 cannot coexist with RK3568's v2 IOMMUs.**
  rk_iommu_probe line 1234 `if (WARN_ON(rk_ops != ops)) return -EINVAL` — the driver has a
  single global `rk_ops`. RK3568 VOP/VDEC use `rockchip,rk3568-iommu`=v2 (40-bit) and probe
  first (rk_ops=v2); our NPU MMU is `rockchip,iommu`=v1 (32-bit, the sub-4GB DTE the NPU
  needs) → v2!=v1 → -EINVAL → IOMMU fails → NPU can't probe. **This is exactly Jonas
  Karlman's IOMMU-thread point (GATE 10): the driver can't support mixed IOMMU revisions
  until per-device ops (Simon Xue's patch) land.** (6.19 board dodged it: its rocket test
  DTB didn't enable the other v2 IOMMUs.)
- **So the conv cannot be tested on clean 7.1-rc6 yet** — blocked on the rockchip-iommu
  per-device-ops work, a separate known upstream item. fw_devlink=permissive didn't help
  (genuine driver EINVAL, not fw_devlink).
- Net for upstreaming: rocket driver (v3) + DT are ready, but full RK3568 NPU bring-up on
  mainline also needs: NPU power-domain DT child, AUTO_GATING IOMMU fix, AND the IOMMU
  per-device-ops support. RFC should disclose this dependency chain.
- Board restored to default l3 (6.19 rocket baseline); l4 (7.1-rc6) entry preserved.
  Memory: `board_7.1_rc6_iommu_coexist_2026_06_02.md`.

### 2026-06-02 (cont.) — v3 RFC series built + gated; NPU status in 7.1-rc6 checked; pivot to board-7.1 test
- Built the **v3 RFC series** (6 patches) on the LXC, addressing all v2 maintainer flames:
  1 introduce per-SoC rocket_soc_data (rk3588 only, no behaviour change); 2 derive DMA width
  + core count from match_data (delete DT rescans); 3 add RK3568 SoC support (PVTPLL/PMU/NOC
  bring-up); 4 binding (FIXED the missing `$ref:` + hardware-altitude commit msg); 5-6 DT.
  GATES PASSED on v6.19-rc5: GATE4 per-commit build (driver+dtb), GATE5 checkpatch --strict
  (0/0/0 all 6), GATE6 dt_binding_check (binding example compiles — the v2 failure fixed),
  GATE9 get_maintainer (Tomeu/Rob/Krzysztof/Conor/Heiko/Oded + lists). Preserved: LXC tag
  `v3-validated-6.19-rc5`; patches in `upstream-prep/v3-patches/`; cover letter
  `upstream-prep/v3-cover-letter.txt` (terse, RFC line 1, honest 3-layer status); binding-fix
  notes `upstream-prep/v3-binding-fix.md`.
- **GATE 10 (IOMMU thread) ANSWERED:** fetched the Anubis-blocked thread via LXC curl. It's our
  own GFP_DMA32 patch; Jonas Karlman's conclusion = don't patch shared v2 IOMMU ops, the RK3568
  NPU MMU is IOMMU v1 (32-bit) needing the v1 compatible (which the DT uses). Resolved, NOT the
  cause (NPU reads with no faults; stall is downstream). Folded into the cover letter.
- **NPU status in latest kernel (v7.1-rc6):** mainline is now v7.1-rc6 (our base 6.19-rc5 is far
  behind). **NO RK3568 rocket support upstream** (not superseded — still ours to add). BUT the
  rocket driver got bugfixes in our edit areas (UAF create_bo, prep_bo return leak, DMA-irq
  status check, error-path unwinding in rocket_probe + rocket_core_init). So v3 must rebase onto
  7.1-rc6 before send (GATE 0). The rockchip-iommu AUTO_GATING fix is NOT upstream in 7.1-rc6.
- **★ USER PIVOT (correct): update the board to 7.1-rc6, test the NPU, then RFC only if needed.**
  If it works on 7.1-rc6 → send a merge candidate (clears Heiko's hard gate); if not → RFC is
  bulletproof. Scope: needs 7.1-rc6 + v3 series + non-upstream board fixes (AUTO_GATING IOMMU,
  v1-compatible NPU-MMU DT, OPP/power) + full kernel build + deploy + boot + test. IN PROGRESS.
- Git caution: concurrent `git fetch` + repeated `git rebase` tangled the branch (fetched
  master `e43ffb69` "Linux 7.1-rc6" differs from tag `v7.1-rc6`=`3bc831df`; rebasing onto the
  fetched master silently DROPPED commits → a bogus "GATE4 pass" that built bare 7.1-rc6).
  Lesson: rebase onto the explicit TAG, verify commit count after, no concurrent fetch.

### 2026-06-02 (cont.) — simultaneous reset-release DISPROVEN; ALL leads exhausted → RFC next
- Implemented `reset_sim` param: in `rocket_full_reset`, release srst_a+srst_h SIMULTANEOUSLY
  via direct CRU write (RK3568 SOFTRST_CON2 @ 0xfdd20000+0x408: SRST_A_NPU id43=bit11,
  SRST_H_NPU id44=bit12; write-mask hi16) per TRM 9.5.2, instead of reset_control's sequential
  deassert. Builds clean; CRU write verified safe (FULL_RESET(sim) logged, board healthy, reads
  still full).
- Tested BOTH forks (full_reset=2 per-job + reset_sim=1, AND full_reset=1 resume + reset_sim=1)
  with BEST STATE: IDENTICAL to baseline — dt_rd=0x3292/wt_rd=0x1900 (full), dt_wr=0, executer
  hangs. **Simultaneous reset release does NOT arm the executer.** The last documented nuance is
  closed.
- **STATE: every documented + software-controllable layer exhausted** (content/byte-exact replay,
  reset incl. simultaneous-release, power, clocks, IOMMU, TRM pipeline-start model). Gate is an
  undocumented HW-internal CMAC↔CBUF↔ACCU handshake. **→ NEXT SESSION: draft the v3 RFC.**
- Board restored to BEST STATE (full_reset=0 reset_sim=0). Params live: reset_sim, full_reset,
  replay_bsp, cmac_bsp, task_pp, bsp_cna, etc. (all default off/baseline).

### 2026-06-02 (cont.) — TRM-deep read on CMAC start: confirms NO missing command-path register
- Read the ACTUAL watermarked PDF (Part2 ch9 NPU, pages 451-462: PPU/DMA/GLOBAL + app-notes +
  conv-flow Figs 9-2..9-6) per user request to double-check the watermark-derived decoded doc.
- Verified: (a) perf counters = DMA BW-summary regs (base 0x8000; 0x8034 feature-WRITE/DPU-out,
  0x8038 feature-READ/CNA-in, 0x803c weight-READ) → dt_wr=0 = DPU output DMA moved 0 bytes;
  (b) `op_en` (block+0x8) = per-block trigger, ping-pong shadowed; (c) **GLOBAL_OP_EN (per-block
  bits) is for the DIRECT conv flow Fig 9-3, NOT PC mode** — PC/slave mode (BSP+rocket) enables
  via `pc_op_enable`, matched; (d) CMAC start config = misc_ctrl+gat_ctrl, **no separate "CMAC
  go" trigger**; (e) status_0=1 on all blocks = all executers OPERATING, none completing =
  deadlock; (f) Reset 9.5.2: both srst ≥32 core-clk, released SIMULTANEOUSLY (rocket deasserts
  sequentially — low-ROI nuance, reset off the working path).
- **Conclusion: the TRM accounts for the entire pipeline-start model; the byte-exact replay
  already reproduced BSP's whole command list + pc_op_enable → NO missing command-path register.
  CMAC operates yet emits nothing → HW-internal CMAC↔CBUF↔ACCU handshake the regs don't expose.**
  Reinforces the RFC. Updated docs/npu-trm-decoded.md (VERIFIED 2026-06-02 section). Memory:
  trm_cmac_start_semantics_2026_06_02.md.
- Only unexercised lead = simultaneous reset-release (bypass reset_control's sequential deassert);
  low ROI. Otherwise: **draft v3 RFC.**

### 2026-06-02 (cont.) — vendor full-reset ported & DISPROVEN (both forks); reset/power layer closed
- Ported vendor `rknpu_soft_reset` as `rocket_full_reset()` (rocket_core.c): msleep(100) +
  CRU assert/deassert + PVTPLL two-step re-init + NoC de-idle; caller re-establishes IOMMU.
  Added `full_reset` param (rocket_drv.c, 0/1/2) + call sites: Fork A resume (rocket_drv.c),
  Fork B per-job (rocket_job.c, with inline IOMMU re-program). 4-file change, builds clean.
- Tested BOTH forks with BEST STATE (bsp_cna): full_reset=1 (resume) AND full_reset=2
  (per-job, "FULL_RESET" log confirmed). IDENTICAL to baseline — dt_rd=0x3292/wt_rd=0x1900
  (full), dt_wr=0, executer hangs. **Vendor full reset does NOT arm the executer.**
- Key framing: vendor's NORMAL power-up is `rknpu_power_on` (regulators+clk+PVTPLL 2-step+
  NoC de-idle+IOMMU trigger_enable) — rocket's resume already mirrors it. `soft_reset` is
  only ACT_RESET-ioctl/timeout, not the working path. So reset wasn't expected on-path, but
  tested per "cover all bases" — negative.
- Also ruled out the vendor's separate `mem`/sram regulator: RK3568 `sram-supply=<&vdd_npu>`
  = same rail as npu-supply (already powered). Not an unpowered CBUF SRAM.
- **Net: content + reset + power + clocks + IOMMU all exhausted. Executer starts (full reads)
  but hangs at compute, with rocket doing everything the vendor does. → RFC strongly
  indicated, OR TRM-deep / vendor consult on CMAC (MAC-array) start semantics.**
- Params live: full_reset (default 0). Board restored to BEST STATE.

### 2026-06-02 (cont.) — ★★★ BYTE-EXACT REPLAY: content DEFINITIVELY ruled out; gate is HW/driver state
- Implemented `replay_bsp`: overwrite rocket's regcmd with BSP's byte-exact 133-entry list
  (from conv2d-bsp.rknn capture), swapping ONLY the 4 real IOVA fields (0x1070 input,
  0x1110 weight, 0x4020 output, 0x5020 dpu_rdma-src) with rocket's harvested BO addresses;
  zero stale trailing mesa entries; AMOUNTS auto-computes to 0x88 (= BSP exact).
- Tested BOTH ping-pong forks (faithful nop=-1 AND off nop=0..5) per "cover all bases":
  **IDENTICAL outcome.** BSP enable scheme landed byte-exact (CMAC 0x200c=1, ACCU 0x300c=0,
  DPU 0x400c=0x108, DPU_RDMA 0x500c=0x27); CNA config VERIFIED LANDED in MMIO (CONV_CON2=
  0x70, WEIGHT_SIZE0=0xc800, WEIGHT_SIZE2=0x5050080, DATA_SIZE all BSP); TASKST=0x11000,
  AMOUNTS=0x88. **Yet dt_wr=0, no execution, out_sum=26214400.** (rocket even reads less:
  dt_rd=0x89, wt_rd=0 — BSP's structure triggers rocket's weight DMA differently.)
- **DEFINITIVE (facts 21–22): the SAME byte-exact command list → full output on BSP, nothing
  on rocket, with config provably identical in HW registers. The gate is categorically NOT
  the command list / values / structure / order / ping-pong. It is the driver's HW-init /
  power / reset / IOMMU state.** Combined with bsp_cna (mesa structure + BSP values → full
  reads, executer STARTS, hangs), content is exhaustively ruled out. Strongest possible proof.
- Gotchas this session: clock-skew makes `make` skip recompiles (rm the .o to force); scp can
  silently no-op on a Tailscale blip (always md5-verify the board copy after scp).
- Memory: updated `executer_hangs_weight_layout_2026_06_02.md` (replay result appended).
  Next: HW-init/power/reset diff (rknpu_drv.c probe + rknpu_reset.c vs rocket) OR RFC.

### 2026-06-02 (cont.) — task_pp DISPROVEN; gate refined to "executer starts but hangs at compute"
- Board recovered (power-cycle + serial hooked up). Ran the staged BSP-combo:
  `bsp_cna=1 cmac_open=1 desc_mode=0 task_pp=1` (TASK_CON bit12 SET + TASKDMA=0, ping-pong
  kept). **Register-level it matched BSP (TASKCON=0x1006, TASKDMA=0) but executer did NOT
  engage:** S_POINTER stuck 0x0e (no bit16), S_STATUS 0x05, dt_wr=0, out_sum=26214400.
  task_pp only flipped TASKST (0x11000↔0x21000). **task_pp_en DISPROVEN as the trigger.**
- Resolved offsets: `RKNPU_OFFSET_PC_DATA_ADDR=0x10`=rocket BASE_ADDRESS → BSP & rocket
  write the regcmd to the SAME reg. BSP's `BASE=0` after OP_EN = PC consumed+cleared it
  (job done); rocket keeps BASE set = PC never completes. Completion symptom, not cause.
- IOMMU detach/attach only fires on job 0 (`attached_domain != job->domain`); jobs 1-4
  skip it and still fail → already effectively attach-once like BSP. NOT the gate.
- **★ Refined gate (TRM S_STATUS decode):** rocket `0x05`=status_0=**1** (executer
  OPERATING) + bit2; BSP `0x0c`=status_0=**0** (executer IDLE = **done**) + bits2,3. So
  rocket's executer **STARTS** (reads full input+weights, status_0=1) but **HANGS
  mid-compute and never completes**; BSP runs to completion. The hang is AFTER compute
  starts → points at the DATA the MAC consumes (weight layout in CBUF) or a HW-arm the
  executer waits on — NOT any PC/submit register (all now match BSP).
- **Leading suspect now: mesa weight-BO PACKING** (rkt_coefs.c, RK3588-derived 32×32 tiles
  / CBUF_BANKS=12 vs RK3568's 8). bsp_cna fixes register VALUES but NOT the BO contents;
  conv2d-bsp.rknn (RKNN-packed) runs on BSP. This is also the required Track-B upstream fix.
- `task_pp` param kept (live, default -1). Memory: updated
  `a3_bsp_executer_engage_2026_06_01.md` lineage via new findings here.

### 2026-06-02 — driver-init diff → task_pp confound found; STAGED test (board went offline)
- Driver-init register-write diff (`srcdiff/`): vendor `rknpu_soft_reset` (NoC/PVTPLL/IOMMU
  re-init, `state_init`=NULL for RK3568) is clocks/IOMMU only — already matched, not the
  arm. The `0xe→0x1004/0x3004` S_POINTER writes are gated `num_irqs>1` (RK3588) — RK3568
  skips them; rocket's equivalent is `if(0)`-disabled. So those match too.
- **★ Found a real confound in TASK_CON.** BSP: `((0x6 | task_pp_en) << 12) | task_number`
  where `task_pp_en` (bit12 = ping-pong TASK mode) is INDEPENDENT of the descriptor.
  Rocket CONFLATED it: `desc_mode` set BOTH the descriptor (TASKDMA) AND bit12. So A4 only
  tested the diagonals — `desc_mode=1`(bit12 set + TASKDMA≠0) and `desc_mode=0`(bit12 clear
  + TASKDMA=0), both failed. **The BSP RK3568 combo — bit12 SET + TASKDMA=0 — is UNTESTED.**
  task_pp_en (PC ping-pong task mode) is a plausible trigger for the executer to latch the
  ping-pong group (= our gate, S_POINTER bit16).
- **STAGED (not yet run — board went offline mid-session):** added `task_pp` param
  (decouples TASK_CON bit12 from desc_mode) to `srcdiff/rocket_job.c`. **Next pickup:**
  push+build, `insmod rocket.ko clean_run=1 bsp_cna=1 cmac_open=1 desc_mode=0 task_pp=1`
  (ping-pong KEPT, nop=-1). Litmus: S_POINTER bit16 sets (0x0e→0x1000e), S_STATUS 0x05→0x0c,
  dt_wr>0. If it fails, reboot l1 to capture BSP's exact TASKCON/TASKDMA (the A3 grep missed
  the DUMP_BEFORE_OPEN main line — re-grep `RKNPU_DUMP_BEFORE_OPEN:` for TASKCON/TASKDMA).
- **BOARD STATUS: OFFLINE** (Tailscale peer down, 100% ping loss, tx>0 rx=0) — dropped
  during local source work, not from a command (the prior `desc_mode=0` run completed fine).
  Needs power-cycle / serial check. LXC compile server (<TAILSCALE-IP>) is up. Board is on l3
  (rocket), kernel #65 module + #68 BSP Image on /boot/Image-6.19 (instrumented). When it's
  back: re-test the staged task_pp combo first.

### 2026-06-01 (cont.) — ★★ A3/A4 BSP side-by-side: gate pinned to HW executer-engage
- Instrumented BSP `rknpu_job_subcore_commit_pc` with the same pipeline sweep + perf
  clear/read (PIPE_IMM/100US/1MS/5MS + CBUF_DBG). Built BSP kernel #68, switched extlinux
  l3→l1, rebooted, ran `conv2d-bsp.rknn` (our EXACT conv). Capture: `logs/bsp-a3/`.
- **BSP `dt_wr=0x6400 = 204800 B = full output`** (DPU writes everything). Reads ≈ rocket
  (`dt_rd=0x32c9`/`wt_rd=0x1900`). CBUF_CON0=0x44 both. BSP all units `S_STATUS=0x0c`,
  `S_POINTER=0x1000e` (HW bit16 set). Answers A1 (BSP dt_wr≠0) as a bonus.
- Switched back l1→l3, rebooted to rocket. **A4:** drove rocket to closest-to-BSP yet —
  `bsp_cna=1 cmac_open=1 desc_mode=0` (TASKDMA=0, TASKST=0x11000 — BSP-like) + ping-pong
  KEPT (nop=-1). Result: rocket `S_POINTER=0x0e` (bit16 NEVER set), `S_STATUS=0x05`,
  `dt_wr=0`. **Entire remaining diff = S_POINTER bit16 (HW-set executer-engage).**
- **Reframe (facts 19–20, §4):** gate is HW-arming the vendor driver does at init/submit,
  outside the regcmd. Command-list + submit config now fully match BSP. Ping-pong on/off
  and desc_mode on/off only shuffle S_STATUS(0x05/0x01)/S_POINTER(0x0e/0)/TASKST — none
  engages the executer. Superseded A2's "CBUF-bank compute gate" (BSP uses same CBUF cfg).
- BSP source instrumented in `srcdiff/rknpu_job.c` (live on board l1 kernel #68; restore
  with a clean rebuild before upstream). Memory: `a3_bsp_executer_engage_2026_06_01.md`.
- **Next (no reboot): driver-init register-write diff** — `rknpu_drv.c` probe +
  `rknpu_reset.c` soft_reset vs `rocket_drv.c`/`rocket_core.c`: NoC/CRU/PMU/IOMMU writes
  to fde40000.npu rocket omits. Hunt the HW-arm that makes S_POINTER bit16 set.

### 2026-06-01 (cont.) — ★ TRACK A2: stall localized to COMPUTE stage (no reboot)
- Added `rocket_pipe_sweep()` helper + 5 call sites (AT_OPEN→AT10MS): samples EVERY
  sub-unit `S_STATUS` (0x_000) + CBUF + run-bits in one line. Closes the long-standing
  instrumentation gap (only CNA+ACCU were sampled over time before).
- **Result (BEST STATE `clean_run=1 bsp_cna=1 cmac_open=1 nop=0..4`):** all 5 units
  (CNA/CMAC/ACCU/DPU/DPU_RDMA) frozen at `S_STATUS=0x1` AT_OPEN→TIMEOUT; never advance.
  `dt_rd=0x328f`(full input) `wt_rd=0x1900`(=51200B full weights) `dt_wr=0`; IOMMU
  returns to idle 0x19 (reads complete, not stalled); PC TASKST=0x21000, no IRQ, 540ms.
- Widened CNA debug scan to 0x1090-0x12fc: CBUF_DBG non-zero (`0x1210=0x01ff0000`,
  `0x1234=0x20`, `0x1280=0x78`, `0x1284=0x00060006`) ⇒ CBUF has content. `0x1110=
  0xffec0000` = DCOMP_ADDR0 (weights). DCOMP_CTRL=0 (uncompressed).
- **Reframe:** the gate is the **CNA→CBUF→CMAC compute hand-off**, NOT DPU-write
  (`dt_wr=0` is a symptom). Updated facts (18), leading hypothesis (§4), next steps.
- TRM note: `data_reuse` is CBUF_CON0 **bit 12** not 13 (my sweep's bit13 print is moot);
  CBUF_DBG regs are at 0x1210+ (what earlier sessions mislabeled "CVT range").
- Source `srcdiff/rocket_job.c` (+`rocket_pipe_sweep`, widened scan); live on board
  (kernel #65, params default off). Memory: `track_a2_compute_stall_2026_06_01.md`.
- **Next:** A3 BSP per-stage compare (reboot) — run conv2d-bsp.rknn on l1 with the same
  sweep; find which unit advances past 0x1 + capture working CBUF_DBG/dc_status/csc_status.

### 2026-06-01 — PC-broadcast PROVEN; content fully exhausted; IOMMU-idle myth busted
- Fixed Tailscale MTU blackhole (set tailscale0 MTU 1000).
- Did the documented source diff: rocket PC programming == BSP byte-for-byte;
  coherency primitives present.
- Found + recorded the byte-exact BSP **OP_ENABLE scheme** and that prior tests of it
  were YOLOv5s-**contaminated** (0xa000 wedge etc.) → clean combo never run. Added
  `op_en_fix` param, ran clean (ping-pong on AND off): **does NOT execute** → op-enable
  scheme disproven. Added `pc_slave_pre` (BSP slave-mode 0x1) → disproven.
- **Power-cycle clean run (cold NPU): PROVED PC broadcasts the regcmd fresh + correct**
  (configs 0→values on cold HW). Stale-readback worry busted.
- **IOMMU "idle" was a misread** — 0x11 has IDLE bit clear; after OP_EN the NPU is
  active/non-idle (cold: 0x1, replay buffer non-empty = pending/stalled). Reframe:
  CNA may be issuing reads that **stall**, not "never reading".
- **Coherency audit (PLAN step 3) — DONE, coherency is correct + NOT the gate.**
  Mesa prep/fini path is right; kernel reads BO RAM directly: INPUT_BO=`81 81…`
  (real), WEIGHT_BO=`e9 0a c9 04…` (real) — both flushed to DRAM correctly. OUTPUT_BO
  pre-job=`00…`; the DPU writes nothing; `out_sum=128`-uniform is TFLite zero_point
  dequant of zeros (corrects the old "DPU writes zero_point" misread).
- Net: content + coherency both closed. Gate is HW-arming the BSP driver does at
  init/submit, outside the regcmd. **Next (no reboot): deep DRIVER register-write diff
  `rknpu_drv.c`/`rknpu_job.c` vs rocket (init+submit writes to fde40000.npu / NoC /
  CRU / PMU).** (l1 capture deprioritized — needs BSP kernel rebuild + 2 remote reboots,
  only confirmatory.)
- Created `PLAN.md` (this file) + README pointer. Memory:
  `op_enable_scheme_contamination_2026_06_01.md`.
- **★ PERF-COUNTER REFRAME (Phase 1 of the approved plan).** Added perf-counter probe
  (clear+enable `0x8010` w/ vendor seq `0x80000101`/`0x00000101`, read `0x8034/38/3c`
  pre+post job) and the PMU `0x60→0x68` poll fix to rocket. Result: **`dt_rd≈0xb3`,
  `wt_rd≈0x38` non-zero → CNA DOES read input+weights; `dt_wr=0` → DPU writes nothing.**
  Overturns the 2-yr "CNA never starts" model — the gate is the **compute→DPU-output
  path**, not CNA-start. Enabling CMAC doesn't make DPU write. PMU bus de-idle confirmed
  fine (`0x60`==`0x68`==`0x1ea`). New `op_en_fix`/`pc_slave_pre`/perf params live in
  rocket.ko (defaults off). **Next: BSP perf compare (reboot) + DPU output-write config
  inspection (no reboot).**
- **★ GATE ISOLATED TO DPU-WRITE (continued 2026-06-01).** `bsp_cna=1` overlay → CNA
  reads FULL input+weights (counter unit=8B: wt_rd=0x1900=51200B full weights,
  dt_rd=0x3292≈full input; was ~1.4 KB with mesa's broken 4× line_stride). But `dt_wr`
  STILL 0 with all 4 units running → **the DPU output-write stage is the sole remaining
  gate.** `op_en_fix` (0x_00c enables) proven HARMFUL (units don't run) → reverted;
  mesa's 0x_008 are the operative enables for rocket. DPU config diff (0x4024/0x40c0 2×,
  0x4040 bit17, 0x4054 trunc) all corrected by bsp_cna yet DPU still writes 0.
  BEST STATE: `clean_run=1 bsp_cna=1 cmac_open=1 nop=0..4`. Memory:
  `perf_counter_reframe_2026_06_01.md`. **Next session: (1) BSP perf compare for our
  conv (reboot) — does BSP's dt_wr!=0? confirms write-stage asymmetry; (2) DPU-write
  trigger hunt — why DPU emits 0 bytes despite enable + full input + correct config
  (CMAC/ACCU actually computing? DPU_RDMA feeding? a write-kickoff bit?).**

### ≤2026-05-31 — (see memory files; summarized)
- Byte-exact BSP vs mesa diff for our exact conv (rknn-toolkit2 1.5.2). Found real mesa
  value bugs (line_stride 4×, CONV_CON4, kernel_group, CBUF_CON1, DMA_CON0/2). Content
  overlay still didn't execute → "content not the gate" (now fully confirmed 06-01).
- 0/5→4/5 sub-units via ping-pong NOP + CMAC open (later shown to be status-reg
  illusion, not real execution). l1 capture reframe: CNA never the bug; no descriptor.
- Full detail in `memory/MEMORY.md` index and the dated topic files therein.
