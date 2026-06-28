# sashiko-bot v4 review triage (2026-06-13)

Patchset: `[RFC PATCH v4 0/9] accel: rocket: Add RK3568 NPU support`
Message-ID: `20260613070116.438906-1-midgy971@gmail.com`
Reviewer model: `gemini/gemini-3.1-pro-preview`. Status: all 9 Reviewed.
Raw: `sashiko-v4-review.json`.

## Author name — resolved, no action
Every review commit header reads `Author: Midgy BALON <midgy971@gmail.com>` (the in-body
`From:` line `git am` records). "MidG971 <...>" is only the **email sender** — Gmail rewrites
the SMTP From display-name to the account name, overriding git's header. Applied authorship is
correct. User chose to keep the form. Nothing to fix.

## Score summary (active / dismissed-as-pre-existing)
| Patch | Active concerns about OUR code | Dismissed (pre-existing rocket bugs, not ours) |
|---|---|---|
| 1 per-SoC soc_data | 0 | 4 (reset-deassert, rdev ERR_PTR, synchronize_irq, pm_get_if_active) |
| 2 derive DMA/cores | **1** | 5 (devres UAF on unbind, drm_dev_register-before-init, rdev locking, ERR_PTR, OOO remove) |
| 3 RK3568 SoC | **3** | 2 (clk_bulk NULL .id, devm_drm_dev_alloc leak) |
| 4 reset-before-detach | 0 | 7 (double-free, iommu_group NULL, pm leaks, irq sync) |
| 5 keep IOMMU attached | **1** | 2 (pm leaks) |
| 6 dt-binding | 0 | 5 (premature register, pm leak, clk NULL id, double-free, NULL domain kref) |
| 7 dts NPU+IOMMU | **2** | 0 |
| 8 dts enable | 0 (No regressions) | 0 |
| 9 pmdomain regulator | 0 | 1 (need_regulator probe-state, handled by Frank Zhang patch in 7.1-rc6) |

**7 active concerns about our code. Everything else = the same pre-existing rocket-driver
issues the v3 bot flagged (Tomeu's code, not introduced by this series).**

## The 7 active items — verdicts (queue for v5; hold until human maintainer feedback)

1. **[P3, fix] `clk_set_rate(600M) || clk_set_rate(1G)` short-circuits.** If the intermediate
   rate fails, the target rate is skipped. Intent is "set both". Real (minor) logic bug.
   → Split into two statements; warn on either failure. EASY.

2. **[P3, CONFIRMED REAL — fix in v5] runtime_resume must re-apply the PVTPLL rate.**
   **Board-verified 2026-06-13 (l3):**
   - The NPU genpd power-domain genuinely powers OFF on runtime-suspend (`pm_genpd_summary`:
     `npu off-0`, `fde40000.npu suspended`). PVTPLL is in that domain → reset on power-off.
   - The board's *working* driver re-applies the rate on EVERY resume: a clean job-free forced
     resume (`echo on > power/control`) prints `rocket: opp_set_rate(1GHz) OK`. v4 dropped this
     (rate change is probe-only in `rk3568_noc_init`; v4 `rocket_device_runtime_resume` only does
     `clk_bulk_prepare_enable`).
   - Not currently *observable* as a failure: `clk_scmi_npu/clk_rate` is the cached SCMI setpoint
     (reads 1 GHz even while suspended — useless as a probe); compute output is gated (uniform-128
     regardless of rate); and the register/AXI path rides `aclk_npu` (CRU 200 MHz), not the PVTPLL,
     so it won't hang. But it's a latent correctness/perf bug once the compute gate is fixed.
   → **v5 fix:** re-apply the SCMI/PVTPLL rate on every `runtime_resume` (two-step 600M→1G to
   defeat the clock framework's unchanged-rate fast-path so TF-A actually re-programs PVTPLL),
   matching the board's proven driver. Cleanest: factor the rate change into a per-SoC callback
   invoked from both probe and runtime_resume, or call it directly in resume after the bulk-clock
   enable. MEDIUM (real, low-risk fix).

3. **[P3, keep+document] `rk3568_noc_init` returns 0 even if the rate change fails.** Deliberate
   warn-and-continue (the clock may already be at rate; failure is non-fatal for probe). Bot
   wants an error return. → Keep; the `dev_warn` is the intended behaviour. Optional: a one-line
   comment saying so. LOW.

4. **[P2, optional hardening] bounds check uses the *current* probe's `soc_data->num_cores`,
   not the array's allocated capacity.** The `cores[]` array is sized from the *first* core's
   soc_data; the `core >= soc_data->num_cores` check reads the *current* device's soc_data. For
   RK3568 (1 core) and RK3588 (all cores share one compatible) these are identical, so it cannot
   overflow in practice. Theoretical only (mixed-compatible DT). → Optional: store the capacity
   in `rdev` and check against that. LOW.

5. **[P5, consider] `rocket_core_fini` detaches the IOMMU without quiescing the HW.** We added
   `rocket_job_fini()` before the detach, but `drm_sched_fini()` only stops the SW scheduler.
   For consistency with patch 4 (reset-before-detach), call `rocket_core_reset()` before the
   detach in fini too. At unbind there is normally no in-flight job, so low impact. LOW-MEDIUM.

6. **[P7, clarify changelog] "assign CLK_NPU so the AXI bus comes up at 200 MHz" vs the literal
   `assigned-clock-rates = <200000000>, <600000000>`.** The bot read this as "CLK_NPU should be
   200 MHz" and suggested swapping. **Do NOT swap** — board-verified that `SCMI_CLK_NPU=200M`
   (compute initial, driver bumps to 1 GHz) + `CLK_NPU=600M` (→ aclk_npu 200 MHz, byte-identical
   to BSP) is correct. The changelog wording invited the confusion. → Reword the commit message
   to state CLK_NPU=600 MHz yields aclk_npu=200 MHz; values are correct as-is. CLARITY.

7. **[P7, clarify] `rockchip,iommu` (v1 ops) on the NPU MMU "should be rockchip,rk3568-iommu (v2)".**
   This is the deliberate v1/GFP_DMA32 sub-4GB-DTE choice, already explained in the cover and
   confirmed by Rockchip (Chaoyi). The per-patch bot didn't see the cover. → Add a short comment
   in the dts node ("v1 ops keep the 32-bit DTE below 4 GiB; see cover / Simon's per-device-ops")
   to preempt the same question from human reviewers. CLARITY.

## Disposition
- v4 was just sent; this is bot output. Per our workflow, **hold** — fold the real items into a
  future v5 only after (or together with) human maintainer feedback. Do not re-roll on bot output
  alone.
- Items 1, 6, 7 are safe, obviously-correct touch-ups worth doing whenever v5 happens.
  **Item 2 is now board-CONFIRMED (2026-06-13) — a real fix for v5** (re-apply PVTPLL rate on
  runtime_resume; the board's proven driver does this, v4 dropped it, and the NPU domain does
  power-cycle). Items 3/4/5 are optional/defensible.
- The large "pre-existing" buckets are rocket-core issues (PM-counter leaks, shared-IRQ vs
  runtime-PM, copy_tasks double-free, NULL job->domain kref, drm_dev_register ordering). Same
  set v3's bot raised. They are Tomeu's to triage; not introduced by this series. Could be
  mentioned to him as a courtesy but are not our patches to fix here.
