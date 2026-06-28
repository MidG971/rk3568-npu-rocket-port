# How to open the RK3568 rocket/Teflon Draft MR (copy-paste)

Tomeu asked for a **Draft** MR at gitlab.freedesktop.org/mesa/mesa to collaborate on
RK3568 (he has no RK3568 HW; you have the ROCK 3B). This is an outward/publishing action —
needs YOUR freedesktop.org GitLab account + push. Materials are all staged:
- Final description to paste: `upstream-prep/mesa-mr/MR-DESCRIPTION.md`
- Commit plan: `upstream-prep/mesa-mr/COMMITS.md` (Option A = one squashed Draft commit)
- The code: `mesa-patches/rocket-rk3568-trm-fixes.patch` (3 files; == what's built/tested on the board)

Build verification vs current mesa main: see "Verification result" at the bottom (filled by Claude).

---

## 0. Prereqs (one-time)
- A freedesktop.org GitLab account: https://gitlab.freedesktop.org/users/sign_in
  (fd.o requires a short access request / approval for new accounts).
- An SSH key uploaded to that account (Settings → SSH Keys), or use HTTPS + a Personal
  Access Token.
- Optional: the `glab` CLI (https://gitlab.com/gitlab-org/cli) to open the MR from the
  terminal; otherwise use the web UI link that `git push` prints.

## 1. Fork mesa (web UI)
Open https://gitlab.freedesktop.org/mesa/mesa → **Fork** → into your namespace.
You get `https://gitlab.freedesktop.org/<you>/mesa`.

## 2. Build the branch (on the board's checkout, ~/mesa)
The board `~/mesa` is the official repo (origin = mesa/mesa) and already has the changes in
its working tree. To produce a CLEAN branch off current main for the MR:

```sh
cd ~/mesa
git fetch origin main
git stash                      # park the working-tree changes (they == the patch)
git checkout -b rk3568-npu-draft origin/main
# Use the MAIN-rebased patch (applies cleanly to current main, no fuzz):
git apply mesa-patches/rocket-rk3568-trm-fixes-MAIN.patch     # the 3 functional files
#   (the original mesa-patches/rocket-rk3568-trm-fixes.patch is the 25.3.0 base and needs
#    `patch -p1 --fuzz=3` on main because of an upstream util_dynarray_append rename; the
#    -MAIN.patch already incorporates that, verified with git apply --check on main.)
# (if the build needs it) the intercept-tool dep fix:
#   the curated patch does NOT touch meson.build; add dep_dl only if you build `intercept`.
git add src/gallium/drivers/rocket/registers.xml \
        src/gallium/drivers/rocket/rkt_ml.h \
        src/gallium/drivers/rocket/rkt_regcmd.c
git commit -F - <<'MSG'
rocket/teflon: add RK3568 NPU conv config support (Draft)

The Rocket Gallium driver / Teflon delegate currently targets the RK3588 NPU.
The RK3568 NPU is the same NVDLA-derived IP but differs in CBUF size, several
register layouts, and a number of encoded config values hard-coded for RK3588.

This collects the TRM-derived, BSP-cross-checked config changes needed to drive
the RK3568 NPU through rkt_regcmd.c: add CONV_CON4 + RGB_BYTELENGTH and expose
FETCH_PIXEL_LEN in registers.xml; fix the FEATURE_MODE_CFG bit layout; set
CBUF_BANKS for RK3568 (256 KiB / 8 banks); emit CONV_CON4 / KERNEL_GROUP /
FETCH_PIXEL_LEN; wake all five sub-units before CNA config; use the RK3568 DCOMP
descriptor layout; and use per-unit OP_EN writes instead of the RK3588 0x81
broadcast.

This is intentionally a Draft and is NOT correct yet: conv2d.tflite still
produces output zero-point. Two independent causes remain (see MR description):
(A) some conv config values are still RK3588-specific, and (B) a kernel-side
HW compute-completion gate (a byte-exact replay of the vendor command list for
the same conv also does not complete on the mainline rocket driver). Opening to
get the userspace divergences on record and discuss per-SoC parameterisation.

Hardware: Radxa ROCK 3B (RK3568). Mesa main. Kernel: rocket with the RK3568 RFC
series applied. cc: Tomeu Vizoso.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
MSG
git remote add fork git@gitlab.freedesktop.org:<you>/mesa.git   # <-- your fork
git push -u fork rk3568-npu-draft
```
`git push` prints a "create merge request" URL — open it.

## 3. Open the Draft MR
- Target: `mesa/mesa` branch `main`. Source: `<you>/mesa` branch `rk3568-npu-draft`.
- Title: `rocket/teflon: add RK3568 NPU support (TRM-derived conv config fixes)`
  and tick **Mark as draft** (or prefix `Draft:`).
- Description: paste `upstream-prep/mesa-mr/MR-DESCRIPTION.md` (it's already MR-ready:
  summary, what's-included, the two known limitations, how-to-test, references).
- cc Tomeu: `@tomeu` in the description (and he's already on the kernel RFC).
- glab alternative:
  `glab mr create -R mesa/mesa --source-branch rk3568-npu-draft --draft \
     --title "rocket/teflon: add RK3568 NPU support (TRM-derived conv config fixes)" \
     --description "$(cat upstream-prep/mesa-mr/MR-DESCRIPTION.md)"`

## 4. After pushing
- Restore the board's working state if you want it back: `cd ~/mesa && git checkout - && git stash pop`
  (or just keep the new branch checked out; the board build can use either).
- Drop the `Co-Authored-By` trailer only if/when moving toward a non-Draft merge
  (COMMITS.md). For a collaboration Draft, keep it.

---

## Verification result (current mesa main) — 2026-06-09
Checked against mesa main `09df2ee2ba9` (board ~/mesa fetch).

- **Patch applies to main: ✅ CLEAN.** The original 2026-05-27 patch (25.3.0 base) failed one
  hunk in rkt_regcmd.c — upstream renamed the 3-arg `util_dynarray_append(regs, uint64_t, X)`
  to a 2-arg form + `util_dynarray_append_typed(...)`, drifting one context line. Re-based onto
  main → **`mesa-patches/rocket-rk3568-trm-fixes-MAIN.patch`**, which `git apply --check`s clean
  on fresh main. Same 3 files / same diff stat (registers.xml 18, rkt_ml.h 6, rkt_regcmd.c 129).
  No stale 3-arg append calls remain.
- **registers.xml → header generation on main: ✅.** Ran main's `gen_header.py --rnn . --xml
  registers.xml c-defines` on the patched XML → RC=0, emits `REG_CNA_CONV_CON4 0x1018`,
  `CNA_CONV_CON4_RGB_BYTELENGTH`, `CNA_DMA_CON0_FETCH_PIXEL_LEN` correctly. (gen_header.py was
  itself changed upstream +12 lines; our additions are compatible.)
- **libteflon.so links: ✅ on mesa 25.3.0** (board, prior sessions, from this exact change).
  **Full ninja link against main: NOT run locally** — the board is Debian 11 / Python 3.9.2 and
  mesa main now requires Python ≥ 3.10; neither LXC has a mesa build toolchain set up. Residual
  C-compile risk is low (emits use only the validated generated macros + `util_dynarray_append_typed`
  which exists on main). **The Draft MR's mesa GitLab CI builds against main automatically** — the
  right place to confirm the link for a collaboration Draft. (Or build on any Python ≥3.10 box.)
- **rocket dir changed upstream 25.3.0..main:** yes — refactor of rkt_ml.c/rkt_task.c/rkt_coefs.c/
  rkt_device.{c,h}/gen_header.py/intercept.c, but **none are touched by this patch**; the only
  collision was the cosmetic append rename, now incorporated in the -MAIN patch.
