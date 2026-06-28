# Plan: assert the HW-internal engage init + roundabout to engage rocket without Rockchip (2026-06-25)

Context: elimination matrix (engage-gate-elimination-matrix.md) proves the engage gate is HW-internal
— every software-visible axis matched to vendor, cold rocket still inert, cold rknpu engages. Two
tracks: (T1) ASSERT what the HW-internal init is (upstream path); (T2) ROUNDABOUT to get a working
NPU now without Rockchip disclosure (interim/local path). T2 is the practical priority.

================================================================================
## TRACK 2 — ROUNDABOUT (get rocket engaging NOW)
================================================================================
Key fact: a bogus job run BY ROCKET cannot prime — rocket's own jobs failing to engage IS the gate.
Priming only works via the vendor rknpu path. The engaged HW state PERSISTS while the NPU power
domain stays on (always-on), so priming is a ONE-TIME boot step.

### 2a. rknpu-as-primer (WORKS today; interim; not upstreamable)
- Boot: `modprobe rknpu` → run ONE tiny job → `rmmod rknpu` → `modprobe rocket`. rocket inherits the
  engaged latch and runs for the rest of uptime.
- Proven by the BSP-primer breakthrough. Make it a systemd oneshot (`npu-prime.service`,
  Before=whatever-uses-rocket). Minimal job = a 1x1 / small conv (engage is model-independent).
- Cost: depends on the out-of-tree vendor rknpu module. Fine for local/demo; NOT for mainline.

### 2b. Self-contained primer = rocket mimics rknpu for ONE priming submit  ★ BRIDGE TO T1
- The ONE structural difference between the two drivers we never isolated: rknpu binds single-region
  (`reg 0xfde40000 0x10000`, uses `base[0]`) + the PLATFORM default IOMMU domain (iommu_dma-managed);
  rocket binds 3-region + creates its OWN unmanaged domain. Both "work" for translation (PC fetches
  regcmd through rocket's domain) — but the CNA engage may depend on the rknpu-style environment.
- Experiment: at rocket probe, do a ONE-TIME priming submit using the rknpu-style path (canned
  regcmd, platform default domain via `iommu_get_domain_for_dev`, single base). If it engages → the
  latch is established → rocket switches to normal ops. If THIS engages, 2b is a fully self-contained,
  potentially-upstreamable primer (no vendor module). If it does NOT engage, it confirms the gate is
  in the rknpu *driver-internal* HW init, not the binding/domain → back to T1.
- This doubles as T1's highest-value experiment (isolates binding/domain vs HW-magic).

### 2c. Minimal canned-regcmd primer via rknpu UABI (slims 2a)
- Instead of full RKNN userspace, a tiny tool submits the canned conv2d-bsp regcmd directly via
  rknpu's DRM_IOCTL_RKNPU_SUBMIT. Still needs rknpu module, but drops the librknnrt/model dependency.

================================================================================
## TRACK 1 — ASSERT the HW-internal init (upstream path, no Rockchip needed yet)
================================================================================
### 1a. rknpu-binding / platform-domain test  (= 2b; TOP PRIORITY, decisive)
Make rocket use the platform default IOMMU domain + single-region base, submit. Engages ⇒ gate was
the domain/binding (fixable in rocket). Doesn't ⇒ gate is rknpu driver-internal HW init (need 1b-1e).

### 1b. Full-register cold-vs-primed diff (extend the existing one)
The current diff (53 regs, EXCLUDING the 0xf008 region that hangs) found only bit16/S_STATUS/config.
Extend to EVERY safe readable NPU offset (sweep 0x0..0x8xxx in small ranges, skip 0xf0xx). Goal: find
any WRITABLE register that differs cold-vs-primed that we haven't replicated (a latch we can set).

### 1c. Primer bisection — what minimal vendor action primes?
- Does rknpu PROBE + a submit that we ABORT before completion prime? (isolates "execution started" vs
  "completed").  - Does a deliberately-failing tiny job prime? - Does priming survive an rknpu
  rmmod+reload (without job)? Narrows "what execution does to the HW".

### 1d. Runtime CRU/PMU/GRF trace (catch dynamic non-NPU writes)
The trace kernel lacks CONFIG_TRACING (clk/regmap tracepoints unavailable). Either rebuild the trace
kernel with CONFIG_FTRACE/CONFIG_TRACING, OR add pr_info around rknpu's clk_set_rate/regmap_write in
the RK3568 path. We read the source (clk two-step + PMU de-idle) but a runtime trace would catch any
conditional/dynamic write the source-read missed.

### 1e. BSP probe audit (all rknpu writel in RK3568 probe path)
We confirmed rknpu_power_on (clk/noc) + state_init=NULL for RK3568. Audit EVERY writel/regmap_write in
rknpu_probe and everything it calls for RK3568 (not just power_on) — look for an NPU/GRF/PMU poke that
isn't in rocket's probe.

================================================================================
## SEQUENCING
================================================================================
1. **Run 1a/2b first** (the binding/platform-domain test) — single decisive experiment that either
   yields a self-contained fix OR definitively pins the gate to rknpu driver-internal HW init.
2. If 1a negative: **2a (rknpu-as-primer systemd oneshot)** = working interim NPU now; in parallel run
   1b (full-register diff) + 1e (BSP probe audit) + 1c (primer bisection) to characterise the init.
3. **1d** if 1b/1e don't surface it (rebuild trace kernel for clk/regmap).
4. Feed findings to gahingwoo/Tomeu; the HW-init question (collab-update-elimination-2026-06-25.md) is
   the upstream blocker if 1a..1e all fail to find a software-settable latch.
