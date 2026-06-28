# BSP RKNPU_WTRACE capture — SET UP 2026-06-15; ROCKET SIDE CAPTURED 2026-06-17

## ✅ L7 READY 2026-06-22b — finalized for the POST-METHOD-B init/IOMMU-SEQUENCE diff
After Method B (gate = rocket kernel driver) + register surface exhausted (CLK_GATE/TASK_CON/etc. all
matched), the ONE remaining avenue = the full module-load→submit MMIO/clk/IOMMU **sequence** diff
(rknn engages, rocket doesn't, all steady-state regs identical → difference must be in init ORDERING).
**L7 is provisioned + verified present 2026-06-22b:** extlinux `label l7` = `Image-6.19-trace` (BSP
RKNPU, WTRACE active) + iommu DTB; `~/capture-wtrace.sh` captures (A) RKNPU_WTRACE NPU reg writes
(boot+submit) + (B) clk/regmap tracepoints (CRU/PMU/GRF/pmdomain). Default left = l3 (NOT armed; flip
when ready).
**▶ CAPTURE PROCEDURE (next session):** `sudo sed -i 's/^default l3/default l7/' /boot/extlinux/extlinux.conf`
→ reboot (serial-attended ideal; l7 uses iommu DTB, networks back) → `~/capture-wtrace.sh` (runs a job,
saves `wtrace-bsp-*.{npu-boot,npu-submit,clk-regmap}.log`) → ALSO `sudo dmesg | grep -iE 'rk_iommu|iommu'
> iommu-seq.log` (⚠️ **IOMMU register writes go via rk_iommu raw writel, NOT regmap — the regmap
tracepoint MISSES them; capture the rk_iommu debug-print sequence from dmesg instead**) → set default
back l3.
**ROCKET SIDE for the diff:** l3 has NO clk/regmap tracepoints (CONFIG_TRACING events absent) → get
rocket's clk/PMU/IOMMU sequence from (a) the `rocket_wtrace` param (NPU reg writes, already added) +
(b) source audit (rk3568_noc_init PMU 0xa0/0x70/0x50, SCMI two-step, rk_iommu attach/trigger_enable
debug prints in dmesg). DIFF the ORDER of {clk-enable, reset-deassert, PMU power-on/de-idle, SCMI rate,
IOMMU attach+enable, NPU submit} rknn-vs-rocket. The engage gate (pre-memory PC↔unit handshake) likely
hinges on a sequence/timing diff here, NOT a register value (all values matched).
**NOTE:** engage is pre-memory (rocket units never read DRAM: dt_rd=PC-fetch-only) so the relevant init
is the PC/NPU enable path, not the BO-mapping path. Focus the diff on clock/reset/power/IOMMU-enable
ORDER relative to the OP_EN.

---

# (historical) BSP RKNPU_WTRACE capture — SET UP 2026-06-15; ROCKET SIDE CAPTURED 2026-06-17

## 🏁 FINAL VERDICT 2026-06-17 — CPU-side submit is IDENTICAL vendor-vs-rocket; gate is NOT in the kernel driver
The (B) non-NPU layer was static-extracted (kernels lack clk/regmap tracepoints) and compared to the
WORKING RKNPU port (l1, drivers/rknpu). Result: **rocket faithfully mirrors the vendor power path.**
- `rk3568_noc_init` == `rknpu_noc_deidle`: same PMU writes (0xa0 pwr-gate, 0x70 auto-idle-off, 0x50
  de-idle), same BUS_IDLE_ST(0x68) bit2 poll. SCMI two-step (600→1000MHz) matches (via OPP). PMU
  power-gate matches.
- **Live state at submit confirms healthy power/NoC/IOMMU:** `BUS_IDLE_ST=0x1ea` (bit2=0, bus active),
  `PWR_STATUS=0x7d` (bit1=0, NPU on), `NOC_AUTO=0xfffb` (auto-idle off), IOMMU `ST=0x19 FA=0` no fault,
  `noc_init ret=0`. Yet `TASKST=0xf000` (PC waiting, units never run).
- Only un-eliminated kernel-side nuance: rocket does a direct CRU clk write (rocket_core.c:259) where
  RKNPU uses the clk framework — LOW probability (clocks demonstrably work: IOMMU AXI + PC run). The
  GRF/PVTPLL writes in rknpu_devfreq_bsp.c are NOT in the working l1 path (l1 uses mainline devfreq).

**⇒ CONCLUSION (rigorous, trace+live-state evidence): everything the CPU does at submit — the 8 NPU
register writes (PC setup + 0x8010 perf-arm) AND the full power/clock/NoC/IOMMU init — is IDENTICAL
between the working vendor driver and rocket, and the HW is powered+de-idled+IOMMU-clean at submit.
There is NO missing CPU-side init.** This DEFINITIVELY closes gahingwoo's "missing RK3568 probe/reset
init" hypothesis (there is none) AND validates the RFC v3 framing (the rocket KERNEL driver is
correct; the executer-engage/compute gate is a SEPARATE problem). Combined with the prior byte-exact
vendor-regcmd replay also failing, the gate localizes to the **regcmd BROADCAST payload (mesa content)
or genuine HW-internal/IP state** — not anything the kernel writes or omits. Consistent with the
2-year "HW-internal" conclusion and gahingwoo's "RK3568 stuck upstream of RK3576."

---

## ✅✅ RESULT 2026-06-17 — BSP captured + DIFFED: NPU-register writes MATCH → gate is the non-NPU (clk/power) layer
BSP l7 capture done by user: `wtrace-bsp-20260617-100509.{npu-boot,npu-submit}.log` (no clk-regmap
log — l7 kernel lacks tracepoints too). Diffed vs `for_review/rocket-wtrace-20260617-095509.log`
(`upstream-prep/wtrace-diff.py` + a set-based offset diff):
- **BSP npu-boot = ZERO RKNPU_WTRACE writes** → vendor does NO probe-time NPU-register writes via this
  hook. No probe-time NPU-register diff exists.
- **Submit writes match** on offsets/values: PC BASE(high IOVA)/AMOUNTS/INT_MASK=0x10300/INT_CLEAR/
  TASK_CON/OP_EN, **and 0x8010 PERF/amount-top = 0x80000101→0x101**.
- **0x8010 was a FALSE POSITIVE in the first diff:** rocket's trace only hooks the 3 `rocket_*_writel`
  MACROS; rocket already writes 0x8010 via a **direct `writel`** (ungated perf block, rocket_job.c
  ~1788, runs every job) → not captured, looked BSP-only. Manually confirmed rocket==BSP on 0x8010.
- Remaining deltas are model-dependent (AMOUNTS 0x88/0x24b vs 0x8a; BASE IOVA; TASK_CON task-number
  0x612f/0x6006 vs 0x7001 incl. rocket's extra bit12 task_pp_en) or benign (BSP writes 0x34
  TASK_DMA_BASE=0; rocket leaves it at reset-0; vendor confirmed no descriptor).
- ⇒ **CONCLUSION: no missing NPU-register write. The RK3568 init gap is in the NON-NPU layer
  (CRU clk / PMU / GRF / PVTPLL / pmdomain) — gahingwoo's #1 hypothesis ("writes outside the NPU
  range").** Next = static-extract (B) both sides (kernels lack clk/regmap tracepoints) and diff
  rocket's power/clock init vs BSP rknpu's power-on path.
- Caveat for any future NPU-reg trace: it MUST also cover direct `writel` paths, not just the macros.

---

## STATUS 2026-06-17 (earlier) — rocket half DONE, only the l7 BSP capture remains
Done this session (board on l3, no reboot, baseline restored):
- **Rocket-side trace built + captured.** Added a gated `rocket_wtrace` module param (0644) that
  logs every CPU->NPU write through the 3 macros (`rocket_pc/cna/core_writel`) as
  `ROCKET_WTRACE <BLK> off=0x.. val=0x..`. The logged offsets are absolute NPU offsets (PC 0x0-based,
  CNA 0x1000, CORE 0x3000) = **directly diffable vs BSP RKNPU_WTRACE**. Splicer: `patch_wtrace.py`.
  Instrumentation gated off by default; `.ko` installed, baseline reloaded.
- **Captured rocket trace:** `for_review/rocket-wtrace-20260617-095509.log`. Rocket's ENTIRE baseline
  CPU->NPU write set per submit = **7 PC writes, nothing else**:
  `0x10 BASE=0xffe7f000(HIGH IOVA) | 0x14 AMOUNTS=0x8a | 0x20 INT_MASK=0x10300 | 0x24 INT_CLEAR=0x300
  | 0x30 TASK_CON=0x7001 | 0x08 OP_EN=1 | 0x08 OP_EN=0`. No CNA/CORE/CMAC/DPU CPU writes (all
  sub-unit config goes via PC regcmd broadcast); no probe-time NPU writes (`rocket_core.c` has 0
  macro calls); no 0x34 TASK_DMA write in this path. ⇒ the diff is pre-narrowed: any BSP direct
  sub-unit / probe / reset NPU write = candidate missing init (exactly gahingwoo's hypothesis).
- **Diff tool ready:** `upstream-prep/wtrace-diff.py` — parses both RKNPU_WTRACE and ROCKET_WTRACE,
  collapses repeated submit cycles, labels offsets by block/register, and reports BSP-only /
  value-mismatch / rocket-only offsets. Self-tested on the rocket log. Usage:
  `./wtrace-diff.py --bsp <bsp.log> --rkt for_review/rocket-wtrace-20260617-095509.log`
- **Non-NPU (B) half gap:** clk/regmap tracepoints are **MISSING** on the l3 rocket kernel (no
  CONFIG_TRACING events) — so rocket's CRU/PMU/GRF/pmdomain writes can't be tracepoint-captured here.
  Options for rocket (B): (a) static-extract from `rocket_drv.c`/`rocket_device.c`/`rocket_core.c`
  (grep `regmap_write`, `clk_set_rate`, pmu/grf `writel`); (b) rebuild rocket kernel with tracing.
  BSP (B) still comes from l7 (capture script handles it, or BSP-source extract).

## ▶ REMAINING (needs l7 boot — serial-attended; board currently default=l3, safe)
1. Reach l7: `sudo sed -i 's/^default l3/default l7/' /boot/extlinux/extlinux.conf` THEN reboot
   (revert after). **Do this with serial attached** — l7 network is uncertain; serial streams the
   probe/init WTRACE at boot regardless (the data we most need).
2. Capture BSP: serial log of boot (probe/init writes) + `~/capture-wtrace.sh` (submit writes).
3. `scp` the BSP log down, then:
   `python3 upstream-prep/wtrace-diff.py --bsp <bsp.npu-boot+submit> --rkt for_review/rocket-wtrace-20260617-095509.log`
4. The **BSP-only offsets** section = the RK3568 init rocket is missing = the gate candidate.
   Implement on rocket (~30s rebuild), re-check S_POINTER bit16 / out_sum.

---

# (original setup, 2026-06-15)

Goal: capture the **working RK3568 BSP** driver's full register-write sequence (esp. probe/
reset-time init) and diff against rocket's, to find the RK3568-specific init step that makes the
executer engage. This is the decisive experiment after gahingwoo's entire RK3576 engage recipe
was exhausted on RK3568 (all negative — see [[jonas_v4_pvtpll_feedback_2026_06_13]] thread /
MEMORY gahingwoo block).

## What's already set up
- **l7 boot entry = `Image-6.19-trace`** (BSP kernel, rknpu built-in with WTRACE) + iommu DTB,
  `log_buf_len=8M` (big buffer, resists rotation), console on ttyS2 @1500000.
- WTRACE is active: `_REG_WRITE` in `drivers/rknpu/rknpu_job.c:25` does
  `pr_info("RKNPU_WTRACE off=0x%x val=0x%x")` on **every** REG_WRITE.
- **extlinux `default l7`** (was l3) — next boot goes to the trace kernel.
  Revert: `sudo sed -i 's/^default l7/default l3/' /boot/extlinux/extlinux.conf`
- **Capture script staged: `~/capture-wtrace.sh`** (updated 2026-06-15) — captures BOTH:
  (A) NPU-block writes via `RKNPU_WTRACE` (boot dmesg + submit), and
  (B) **non-NPU writes** via `clk` + `regmap` tracepoints (CRU clock ungate/rate/source + PMU/GRF/
  pmdomain pokes) — per gahingwoo's #1, the missing RK3568 init is likely *here*, not in the NPU
  block, and would never show as an "NPU register" diff. Outputs `~/wtrace-bsp-<ts>.{npu-boot,
  npu-submit,clk-regmap}.log`. Drives YOLOv5s (known-good; init writes are model-independent).
  If the clk/regmap tracepoints are missing (kernel lacks CONFIG_TRACING events), the script warns
  — fallback: add `trace_event=clk:clk_enable,clk:clk_set_rate,regmap:regmap_reg_write
  trace_buf_size=8M` to l7's append line for boot-time tracing.

## gahingwoo's diff guidance (2026-06-15, what surfaced the RK3576 gate)
He confirmed he skipped NO probe/reset write; `0001..0010` is the whole RK3576 recipe, and the
INT_MASK value only fell out of byte-diffing the vendor submit. Treat this diff as the MAIN line.
Look specifically for:
1. **Writes OUTSIDE 0x2770xxxx (NPU range) — i.e. CRU/PMU/GRF/pmdomain.** An NPU enable / clock
   ungate / PLL/PVTPLL source select the rocket port misses. (Capture (B) above targets this.)
2. **Exact `INT_MASK`/`INT_CLEAR`.** Our BSP = `0x10300` (bit16|0x300); RK3576 = `0x300`. bit16 is
   the executer-engaged status bit — some NVDLA blocks gate the status bit behind its own mask, so
   writing bit16 into INT_MASK may be a *prerequisite* on RK3568. (We tested INT_MASK=INT_CLEAR=
   0x10300 → still no engage; re-confirm against the live trace — the property is the exact value,
   not "MASK==CLEAR".)
3. **Reset/clock ORDERING** relative to clock-enable + IOMMU-attach + PD. RK3576 order: reset →
   clocks → IOMMU → PD → submit. Executer sits armed-but-idle if CBUF/core clocks aren't already
   running at submit time.
Also watch the PC during the job — `PC_TASK_STATUS` + amount reg: does PC dispatch+wait, or drain
the command list and report done instantly? (Instant drain = trigger never reached the units →
points at (1)/(2), not the regcmd.) NOTE: our rocket PC reaches TASKST=0xf000 (operating/WAITING,
not instant-done 0x11000) — so the trigger reaches the PC's wait state but units never run, which
already points at the init (1)/(2) over the command list.

## Capture procedure (next session)
**Wedge-proof path (recommended — survives the WiFi-wedge-on-reboot):** capture via SERIAL.
1. On host: `picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/wtrace-bsp-boot-$(date +%F).log`
2. Reboot the board (it boots l7). The **probe/init WTRACE streams to the serial log at boot**
   automatically (console=ttyS2) — this is the init data we most need, captured before any job.
3. If WiFi comes back: `ssh radxa@<ip> '~/capture-wtrace.sh'` then `scp` the `wtrace-bsp-*.log`.
   If WiFi wedged: run the YOLOv5s demo over the serial session; the submit WTRACE streams too.

**SSH path (if WiFi survives):** `ssh … '~/capture-wtrace.sh'`, then `scp ~/wtrace-bsp-*.log`.

## Then: rocket side + diff
- Boot l3 (pick at menu or set default back). Rocket does NOT use RKNPU_WTRACE; to get a
  comparable trace, add a one-line `pr_info` to `rocket_pc_writel`/`rocket_cna_writel`/etc. (or
  extract the probe/reset writes from rocket source) → capture rocket's probe/init writes.
- **Diff:** reduce both to ordered `off=0x.. val=0x..` pairs; the BSP-only writes at PROBE/RESET
  (before first submit) = the RK3568-specific init rocket is missing. That's the gate.

## Why this is the decisive experiment
Every submit-path lever is matched to BSP/RK3576 and still no engage (descriptor, INT_MASK==
INT_CLEAR, S_POINTER arm, op_en, enable_mask, pp_state_init). BSP RK3568 runs the same submit and
works → the difference is init-time, which only this diff surfaces. After it: implement the
missing init in rocket (board, ~30s module rebuild) and re-check S_POINTER bit16 / out_sum.

## Board state at setup
On l3, healthy; rocket instrumented (ALL_SP dump, pp_init=1, desc_mode=1, params intact).
default=l7 armed. conv2d-bsp.rknn + yolov5s rknn present.
