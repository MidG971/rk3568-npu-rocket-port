# Upstream Prep — Rocket RK3568 NPU Port

Drafts and notes for upstream submission. See `STATUS.md` for the
comprehensive 12-day investigation report.

## Files

- `STATUS.md` — Full 12-day findings, current state, suggested submission order
- `v2-cover-letter-DRAFT.txt` — Updated cover letter for the existing
  rocket-rk3568 kernel patch series (refresh of April's `0000-cover-letter.patch`)
- `mesa-rfc-DRAFT.md` — RFC describing mesa rocket fixes needed for RK3568
  (OP_ENABLE offset bug + 5 other findings)

## Related artifacts in repo

- `../patches/rocket-rk3568/` — The actual 5-patch kernel series (Apr 4 2026,
  still applies). Patches 1-5 are clean; only cover letter needs refresh
  (see `v2-cover-letter-DRAFT.txt`).
- `../mesa-patches/rocket-rk3568-trm-fixes.patch` — Existing mesa patch
  with TRM-derived fixes (CONV_CON4, FETCH_PIXEL_LEN, FEATURE_MODE_CFG,
  CBUF_BANKS, individual sub-unit OP_EN emits). The OP_ENABLE-offset bug
  from yesterday's finding is NOT yet in this patch — needs adding.
- `../bsp-userspace-recon/librknnrt-rk356x.so` — Closed-source BSP userspace
  for reference (5.2 MB stripped aarch64 ELF)

## Action checklist for actually sending

### Kernel rocket patch series (v2)

1. Verify patches still apply to mainline 6.19/6.20 — checkout latest, run
   `git am --3way patches/rocket-rk3568/000*.patch`
2. If anything conflicts, refresh with `git rebase`. Per Apr 4 history, the
   driver loads cleanly so changes should be minimal.
3. Replace cover letter `0000-cover-letter.patch` content with
   `v2-cover-letter-DRAFT.txt` (Date, Subject already updated)
4. Sign-off check, dtbs_check, `scripts/checkpatch.pl --strict patches/*.patch`
5. Send with `git send-email --to=...` per cover letter's recipient list

### Mesa RFC

1. Confirm mesa-dev mailing list posting protocol (lurk a recent thread)
2. Convert `mesa-rfc-DRAFT.md` to plain text email
3. Send to mesa-dev@lists.freedesktop.org + CC Tomeu directly
4. Reference: the kernel series posting (if it's gone out by then)
5. Attach BSP regcmd captures as `.txt` artifacts if list allows

### What WE STILL DON'T HAVE for shipping

- A clean mesa source tree with the OP_ENABLE-offset patch applied,
  built, tested to verify the patch as a real change (not just textual).
- Confirmation that the v1 GFP_DMA32 IOMMU patch's blocker (Simon Xue's
  per-device-ops patch) has landed in mainline — if it has, we can
  refresh and resubmit the GFP_DMA32 patch as a follow-on.

### What we DO have

- Kernel rocket 5-patch series ready (April; rebase if needed)
- Comprehensive findings document
- Empirical evidence (BSP captures + rocket captures, side-by-side)
- Live testbed (Radxa ROCK 3B on l3) with all patches applied

## Notes

The mesa RFC discloses the conv-doesn't-fire issue openly. This is the
right approach: maintainers can correctly assess whether to merge fixes
that improve the situation even if not 100% complete, and we get
useful expert feedback on the remaining bug.

The kernel rocket series is "driver loads + IOMMU works"-scope — that
work is solid and independently valuable. Conv execution is a separate
problem.
