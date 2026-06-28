# Pre-Submission Checklist — kernel patches

A mechanical, copy-paste runbook to run **before every `git send-email`**.
Born from the v2 RK3568-rocket review (2026-05-29), where Heiko and
Krzysztof correctly flamed us for: an untested binding (`$ref:` missing),
a mislabelled version, driver-centric commit prose, an over-stuffed
patch, dead DT-scanning code, and a novel-length AI cover letter.

Each gate below maps to one of those failures. **Do not skip a gate just
because "it built last time."** The rule of thumb: if you cannot paste the
command output proving a gate passed, it did not pass.

Tree locations:
- Compile server: `<COMPILE-SERVER-IP>` (Proxmox) → LXC `<TAILSCALE-IP>`,
  tree `/root/rocket-upstream/mainline/`
- Board (functional test): `radxa@$(cat ~/.rock3b_ip)`

---

## GATE 0 — Rebase onto the right tree FIRST

Patches age badly (v1 didn't apply to rc5). Rocket goes through
**drm-misc-next**, DT through the rockchip tree, but for review always
base on current `linux-next` or the subsystem's `-next`.

```bash
git remote -v                      # confirm drm-misc / linux-next present
git fetch <remote>
git rebase <remote>/drm-misc-next  # or linux-next/master
```

✅ Pass: `git status` shows you are N commits ahead of a *named upstream
ref dated within the last week*, not a stale tag.

---

## GATE 1 — Readiness: does it actually work?  (the Heiko gate)

> "We don't want to merge partially working code. Either things work, or
> they don't." — Heiko, 2026-05-29

**This is a hard gate, not advice.** For the rocket driver specifically:
end-to-end inference must produce output matching the CPU reference
(conv2d.tflite: `sum=23,252,430`) on the board. Until then, the driver
series does **not** go to the list as a merge candidate.

Allowed exceptions (must be labelled `RFC` in the subject prefix):
- You are explicitly soliciting design feedback, not merge.
- The cover letter states up front "RFC, not for merge" in the first line.

```bash
# On the board, after installing the kernel + DTB under test:
ssh radxa@$(cat ~/.rock3b_ip) "ls -l /dev/accel/accel0"
ssh radxa@$(cat ~/.rock3b_ip) "dmesg | grep -i 'rocket\|NPU core'"
ssh radxa@$(cat ~/.rock3b_ip) "python3 ~/npu-debug/teflon_test.py"   # compare to ref
```

✅ Pass: output sum == reference, OR subject carries `RFC` and cover letter
says so in line 1.

❌ If it doesn't work and you're not sending RFC: **stop here.** Nothing
below matters yet.

---

## GATE 2 — Patch granularity (the bullet-point test)

> "If you need bullet points to describe your patch, that strongly
> indicates these need to be multiple patches." — Heiko

Read each commit message body. **If the body enumerates independent
changes (1./2./3. or bullet list), split it.**

Concrete split for the RK3568 series (per Heiko's direction):
1. `accel: rocket: Introduce per-SoC rocket_soc_data`
   — abstraction only, **rk3588 data only**, no behaviour change.
2. `accel: rocket: Derive DMA width and core count from match data`
   — replace the DT rescans (Heiko's point 5).
3. `accel: rocket: Add RK3568 SoC support`
   — rk3568_soc_data + rk3568_noc_init (PVTPLL / PWR_GATE / NOC de-idle).
4. dt-bindings …
5. dts: add nodes …
6. dts: enable on ROCK 3B …

✅ Pass: every commit body describes **one** change and needs **no**
enumerated list to justify it.

---

## GATE 3 — Commit-message & cover-letter altitude + length

> "You need to start explaining the hardware, not drivers." — Krzysztof
> "Please reduce those novel-sized (generated) texts." — Heiko

Rules:
- **Bindings commits describe HARDWARE**, never "the driver consumes…".
  Wrong: "rockchip,pmu is consumed by the driver to issue PMU writes."
  Right: "The RK3568 NPU shares NOC bus-idle control with the PMU; the
  phandle points to that syscon."
- **Driver commits describe the change & the why**, concisely. Imperative
  mood, present tense.
- **No AI-essay tells**: no "Most notably…", no "comprehensive", no
  three-paragraph rationale where one sentence works. Write it by hand.
- **Cover letter ≤ ~25 lines of prose.** What/why/test/changelog. Done.
- Drop marketing lines like "Assisted by Claude … throughout." Keep
  attribution, if any, to a single `Co-developed-by`/`Suggested-by` trailer
  only where literally true.

✅ Pass: read each message aloud in under 30 seconds; a binding message
mentions no driver internals.

---

## GATE 4 — Per-patch build (bisectability)

Every commit must build on its own (so `git bisect` works).

```bash
cd /root/rocket-upstream/mainline
git rebase <base> --exec \
  "make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j56 \
       drivers/accel/rocket/ && \
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j56 \
       rockchip/rk3568-rock-3b.dtb"
```

✅ Pass: rebase completes; **no commit fails the exec**.

---

## GATE 5 — checkpatch (every patch, --strict)

```bash
git format-patch <base> -o /tmp/series
./scripts/checkpatch.pl --strict /tmp/series/*.patch
```

✅ Pass: `0 errors, 0 warnings, 0 checks` on each. Fix or consciously
justify every CHECK.

---

## GATE 6 — Binding validation (the gate we FAILED)

This is the one that caught us. **`dtbs_check` is NOT enough** — it
validates DTBs, not the schema file. You MUST run `dt_binding_check` on
the exact changed `.yaml`:

```bash
# 1. lint the YAML itself
yamllint Documentation/devicetree/bindings/npu/rockchip,rk3588-rknn-core.yaml

# 2. validate the binding schema (CATCHES missing $ref, bad types, typos)
make ARCH=arm64 dt_binding_check \
  DT_SCHEMA_FILES=Documentation/devicetree/bindings/npu/rockchip,rk3588-rknn-core.yaml

# 3. validate DTBs against schema (your boards + the example in the binding)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs_check \
  DT_SCHEMA_FILES=Documentation/devicetree/bindings/npu/rockchip,rk3588-rknn-core.yaml
```

First run needs `pip3 install dtschema yamllint` and a recent `dt-schema`.

✅ Pass: step 2 prints **zero** errors mentioning your file. If you didn't
run step 2, you did not test the binding — period.

Property reminder for next time:
```yaml
  rockchip,pmu:
    $ref: /schemas/types.yaml#/definitions/phandle   # <-- the $ref we dropped
    description: ...
```

---

## GATE 7 — Versioning sanity (the Krzysztof gate)

> "There was never a v1. v1 is the first version you send, regardless of
> its status." — Krzysztof

Rules:
- **The first series you put on a list is v1.** Internal drafts don't
  count. Never label a first on-list send "v2".
- Once a version is on-list, the next is the next integer. We already sent
  "v2", so the *next* corrected series is **v3** (you do not relabel a
  sent series — you move forward).
- Put the changelog **under the `---` scissors** (per-patch) and in the
  cover letter, newest version on top.
- Use `git format-patch -v3 --cover-letter`.

✅ Pass: version number reflects real on-list history; changelog present
below `---`.

---

## GATE 8 — Self-send dry run + apply test

Catch threading, From:, and apply problems before the world sees them.

```bash
# dry run shows recipients & threading without sending
git send-email --dry-run --to=... --cc=... /tmp/series/*.patch

# send ONLY to yourself first
git send-email --to=midgy971@gmail.com /tmp/series/*.patch
```

Then, from a clean checkout, apply your own mail with `b4`:
```bash
b4 am <message-id>        # or: b4 shazam <message-id>
git am ./*.mbx
```

✅ Pass: `b4 am` + `git am` apply cleanly on top of GATE 0's base.

---

## GATE 9 — Recipient list from the tree, not memory

```bash
./scripts/get_maintainer.pl /tmp/series/*.patch
```

✅ Pass: To:/Cc: match `get_maintainer.pl` output (maintainers To:, lists
+ reviewers Cc:). Don't hand-curate from an old memory file.

---

## GATE 10 — Address the open IOMMU question (series-specific)

Heiko asked whether the rknpu IOMMU thread
(lore.kernel.org/linux-rockchip/5663593b-...@rock-chips.com) bears on our
not-working state. Before resubmitting:
- Read that thread; determine if Simon's per-device-ops / v1-vs-v2 IOMMU
  work changes our DTE/page-walker behaviour.
- Reply to Heiko on-list with the answer (even if "no, here's why").

---

## One-line gate summary (run top to bottom)

```
0 rebased on fresh -next        6 dt_binding_check on the .yaml  ← we failed
1 inference works (or RFC)      7 version = real on-list history
2 one change per patch          8 self-send + b4 apply clean
3 hardware-altitude, terse      9 get_maintainer.pl recipients
4 every commit builds          10 answered the IOMMU question
5 checkpatch --strict clean
```

If any line lacks pasted proof, the series is **not ready to send.**
