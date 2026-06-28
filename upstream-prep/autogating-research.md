# RK356x v1 IOMMU `RK_MMU_AUTO_GATING` — register-value research

**Date:** 2026-06-04
**Task:** Resolve the contested value/semantics of the write
`rk_iommu_write(base, RK_MMU_AUTO_GATING /*0x24*/, 0x2)` added to
`rk_iommu_enable()` in `drivers/iommu/rockchip-iommu.c`.
**Status:** READ-ONLY research. No source/board/build modified.

---

## TL;DR (the verdict)

1. **The code and its comment contradict each other.** The comment says
   "clear bit 1 (page-walker auto clock-gating), keep bit 0 (slave-port gate)".
   The intent described requires value **`0x1`** (`0b01` = bit0 set, bit1 clear).
   The code writes **`0x2`** (`0b10` = bit0 *clear*, bit1 *set*) — the exact
   opposite of the comment on **both** bits.

2. **The "only 0x2 works" empirical claim is NOT trustworthy.** It comes from a
   2026-05-12 bisection that was later **explicitly contradicted and reverted**
   (2026-05-15/05-17). The careful comparison test found `0x2` actually
   *disabled the page walker entirely* — it masked the real bug and **broke the
   known-good vendor RKNPU path** (YOLOv5s failed with `0x2`; reverting it
   restored YOLOv5s at 55.8 ms/loop). See section (e).

3. **There is no authoritative source for the bit semantics.** The RK3568 public
   TRM does **not** document the IOMMU/MMU register block at all (no DTE_ADDR,
   STATUS, or AUTO_GATING entries). The vendor BSP `rockchip-iommu.c` is **not
   present** on the board (only an rknpu sparse-checkout is), and the vendor
   **RKNPU driver does not write AUTO_GATING at all** — so STATUS.md's claim that
   "the vendor RKNPU driver writes RK_MMU_AUTO_GATING = 0x2" is **factually
   incorrect**.

4. **Recommendation: do NOT submit this patch as-is.** Either drop it (the
   register is HW-default `0x3` and the known-good vendor path runs fine on that
   default) or, if a write is genuinely needed, the only value consistent with
   the *stated intent* is a **read-modify-write that clears bit 1 and preserves
   bit 0** — which on the reset value `0x3` yields **`0x1`**, not `0x2`. This
   must be re-validated on the board before submission (see section (c)).

---

## (a) What the vendor BSP writes to AUTO_GATING

**Finding: nothing observable here writes it, and the public TRM doesn't define it.**

### Public TRM — no IOMMU register documentation
Searched all 852 RK3568 TRM Part-1 PDF chunks and the Part-2 set under
`trm/` and `trm/part_2/`:

- `pdftotext | grep -i auto_gating` → hits are **DDR/clock** registers only
  (PDF chunks 217, 218, 473, 492, 776 — `ddrc_core_clk auto gating`,
  `aclk auto gating`, `ddrphy_auto_gating_ena`). None is the IOMMU.
- `grep -iE 'MMU_DTE_ADDR|MMU_AUTO|MMU_STATUS|PAGING_ENABLED|MMU_COMMAND'`
  → **0 hits** across the entire TRM.
- No "IOMMU" / "memory management unit register" chapter exists in Part-1.

> Conclusion: Rockchip does not publish the IOMMU MMU register map. AUTO_GATING
> bit semantics **cannot** be cited from the TRM.

### Project TRM extracts (`docs/npu-trm-decoded.md`, `docs/npu-trm-registers.md`)
Only mention NPU-internal clock gating (`CNA_clk_gate` @0x0090,
`CMAC_gat_ctrl` @0x0010, etc.). These are **NPU sub-block** registers, not the
IOMMU's `MMU_AUTO_GATING` @0x24. Not relevant to this question.

### Vendor BSP RKNPU driver (board: `~/npu-port/bsp-driver-tmp`, branch `develop-5.10`)
- `grep -rni 'auto_gating|AUTO_GATING|gating'` over `drivers/rknpu/` → **0 hits**.
- `rknpu_iommu.c` uses only the generic IOMMU API (`iommu_map`,
  `rknpu_iommu_dma_map_sg`, `iommu_get_domain_for_dev`); it never `writel`s an
  MMU register offset. The only `0x24` in the BSP rknpu tree is a **perf-counter
  offset** (`offset_dt_rd = 0x2438`, unrelated NPU register).
- **The RKNPU driver never touches `RK_MMU_AUTO_GATING`.** Whatever the vendor
  does (if anything) lives in the vendor `rockchip-iommu.c`, which is **not on
  the board** (the BSP checkout is a sparse rknpu-only tree).

> Therefore STATUS.md line 66 / CLAUDE.md 10a ("vendor RKNPU driver writes
> RK_MMU_AUTO_GATING = 0x2 ... vendor BSP driver writes AUTO_GATING in its power
> path") is **unsubstantiated** by anything reachable. Verbatim BSP code for the
> register could not be quoted because no such code exists on the board.
> **Action item:** to settle authoritatively, obtain the RK356x BSP
> `drivers/iommu/rockchip-iommu.c` (kernel 5.10, `rockchip/develop-5.10`) and
> grep it for `MMU_AUTO_GATING` / offset `0x24`.

---

## (b) Correct bit semantics for RK356x v1

From the code's own `#define RK_MMU_AUTO_GATING 0x24` and the project notes
(which are self-consistent on the *naming* even where they err on the *value*):

| bit | name (per project notes) | effect when **set (1)** |
|-----|--------------------------|-------------------------|
| 0   | slave / register-port auto clock-gating | gates the register (slave) port clock when idle; notes claim "must remain set; clearing it drops DTE_ADDR" |
| 1   | page-table-walker auto clock-gating | gates the walker's AXI-master clock between transactions; on a TLB-miss walk the DTE fetch can stall |
| —   | reset value | **0x3** (both bits set) |

The *intended* fix for the alleged walker stall is: **keep the walker clock
ungated** → **clear bit 1**, while **preserving bit 0** (slave gate) → result
`0b01 = 0x1`.

**The written value `0x2` does the opposite on both bits**: it *enables* walker
gating (bit1=1, the very thing the comment says it wants to disable) and
*disables* the slave gate (bit0=0, which the notes say must stay set). This is
internally inconsistent and is the source of the reviewer's flag.

⚠️ These bit names are **project-derived/empirical, not TRM-confirmed.** It is
entirely possible the bit polarity is inverted from the assumption (e.g. some
Rockchip "gating" fields are "1 = disable gating"), which would make `0x2`
accidentally "work" for a different reason than the comment states. Without the
vendor `rockchip-iommu.c` or the IOMMU TRM, the polarity is unverified.

---

## (c) Recommended value: `0x1` vs `0x2`, and RMW?

**Primary recommendation: drop the write for the upstream series.**
- Pristine upstream (Linux 6.19-rc5) never writes `RK_MMU_AUTO_GATING`
  (confirmed: only the `#define` at line 42; `grep -c rk_iommu_write.*AUTO_GATING`
  on `0f61b18:drivers/iommu/rockchip-iommu.c` = **0**), and no other Rockchip SoC
  path writes it.
- The HW reset value `0x3` is what the **known-good vendor RKNPU path runs on**,
  and CLAUDE.md confirms RKNPU YOLOv5s works at 55.8 ms with the AUTO_GATING fix
  fully removed. So the default is demonstrably fine for a working IOMMU.
- The rocket NPU's remaining failure is the HW *compute* gate (uniform-128
  output), which is unrelated to the IOMMU walker and is **not fixed by any
  AUTO_GATING value**.

**If a write is retained** (e.g. a separate, board-justified IOMMU patch):
- Use the value that matches the stated intent: **`0x1`** (clear bit 1, keep
  bit 0), **not `0x2`**.
- Use a **read-modify-write**, not a blind store, so unknown reserved bits are
  preserved and the change is explicit:
  ```c
  u32 v = rk_iommu_read(base, RK_MMU_AUTO_GATING);
  v &= ~BIT(1);          /* clear page-walker auto-gating */
  /* bit 0 (slave gate) left untouched */
  rk_iommu_write(base, RK_MMU_AUTO_GATING, v);
  ```
  (On reset value 0x3 this yields 0x1.)
- **This MUST be re-validated on the board first** (see "board test" below).
  The current `0x2` value was "verified live" only by reading back `0x2` and
  seeing `ST=0x19`, which proves the write landed, not that it's correct — and
  the later revert showed `0x2` is actively harmful on the working path.

---

## (d) How to gate the write to v1 ops only

Post Simon Xue's "Drop global rk_ops in favor of per-device ops" commit
(`e9f41b705ea64`), the driver stores per-instance ops:

- `struct rk_iommu { ... const struct rk_iommu_ops *rk_ops; }` (line ~100)
- Set in probe: `iommu->rk_ops = of_device_get_match_data(dev);` (line ~1228)
- Two op tables exist:
  ```c
  static struct rk_iommu_ops iommu_data_ops_v1 = { ... .dma_bit_mask = DMA_BIT_MASK(32), .gfp_flags = GFP_DMA32, };
  static struct rk_iommu_ops iommu_data_ops_v2 = { ... .dma_bit_mask = DMA_BIT_MASK(40), .gfp_flags = 0, };
  ```
- Bound via `of_device_id rk_iommu_dt_ids[]`:
  `"rockchip,iommu"` → `&iommu_data_ops_v1`;
  `"rockchip,rk3568-iommu"` → `&iommu_data_ops_v2`.

**Gating options (cleanest first):**
1. Direct pointer compare in `rk_iommu_enable()`:
   ```c
   if (iommu->rk_ops == &iommu_data_ops_v1)
       /* RMW AUTO_GATING here */;
   ```
   (`iommu_data_ops_v1` would need to be declared/visible above the use site, or
   forward-declared — both tables are currently defined near the bottom.)
2. Cleaner/extensible: add a bool flag to `struct rk_iommu_ops`
   (e.g. `bool clear_walker_gating;`), set it `true` in `iommu_data_ops_v1` only,
   and test `iommu->rk_ops->clear_walker_gating`. This avoids referencing the
   table symbol from the enable path and reads well in review.

The RK356x v1 IOMMU is exactly the `"rockchip,iommu"` compatible (NPU MMU,
vicap MMU), which is `iommu_data_ops_v1` — so either gate restricts the write to
the intended hardware.

---

## (e) Supporting evidence / quotes

**1. The contradiction in our own commit (LXC `/root/rocket-upstream/mainline`,
commit `7ee119e` / current HEAD lines 956-966):**
```c
ret = rk_iommu_enable_paging(iommu);
if (!ret) {
    /* ... Clear bit 1 (keep bit 0, the slave-port gate) once paging is enabled. */
    for (i = 0; i < iommu->num_mmu; i++)
        rk_iommu_write(iommu->bases[i], RK_MMU_AUTO_GATING, 0x2);   /* sets bit1, clears bit0 */
}
```
Commit message (`/root/msg_autogating.txt`): "Clear bit 1 (keeping bit 0...)".
→ comment/message say clear-bit-1/keep-bit-0; value `0x2` does set-bit-1/clear-bit-0.

**2. Pristine upstream never writes it** (LXC):
- `git show 0f61b1860cc3f:.../rockchip-iommu.c | grep -n AUTO_GATING`
  → only `42:#define RK_MMU_AUTO_GATING 0x24`.
- write count = **0**.
- `git log -p --all -S RK_MMU_AUTO_GATING` shows **both** AUTO_GATING-write
  commits are ours (`Midgy BALON`: `043be7a` "board-test only", `7ee119e`
  upstream-styled). No upstream/vendor commit introduces it.

**3. The "0x2 works" claim and its retraction (CLAUDE.md):**
- 10a (2026-05-12), the origin of "only value 0x2 works":
  > "bit 0 = register-port (slave) auto-gating; **must remain set** — writing 0
  > to bit 0 clears the DTE_ADDR register. ... only value 0x2 works."
  (Self-contradictory: `0x2` *clears* bit 0, the bit it says must stay set.)
- Lines 661-673 (2026-05-17), the retraction:
  > "Kernel state: #61 with AUTO_GATING fix **fully removed**. ... RKNPU YOLOv5s
  > WORKS again at 55.8ms/loop (l1), IRQ count 33."
  > "The AUTO_GATING=0x2 'fix' was actually **disabling the page walker** →
  > translation broken → STATUS never went to 0x01 → bug invisible."
- Line 700 (2026-05-15):
  > "AUTO_GATING=0x2 was MASKING the real bug ... booted l1 (rknpu) under kernel
  > with AUTO_GATING=0x2 → YOLOv5s **also failed** ... The 0x2 value actually
  > **disables the page walker entirely**."

**4. Vendor RKNPU does not write AUTO_GATING** (board `bsp-driver-tmp`):
`grep -rni gating drivers/rknpu/` → 0 hits; `rknpu_iommu.c` uses generic IOMMU
API only. (STATUS.md's "vendor RKNPU writes 0x2" is unsupported.)

**5. No TRM definition:** 852 Part-1 PDFs + Part-2 set contain no IOMMU MMU
register documentation (`MMU_DTE_ADDR|MMU_AUTO|PAGING_ENABLED` → 0 hits).

---

## What a board test must check (to settle conclusively)

The evidence base is **inconclusive on the true bit polarity** because the only
authoritative sources (vendor `rockchip-iommu.c`, IOMMU TRM) are unavailable.
A board test on the **v1 IOMMU** (NPU MMU) should, for each candidate value
`{default 0x3, 0x1, 0x2}`:

1. Read back `RK_MMU_AUTO_GATING` (0x24) after `enable_paging` to confirm which
   bits are writable / sticky (does bit 0 actually drop DTE_ADDR? does it stick?).
2. Run the **known-good vendor RKNPU YOLOv5s** path under each value and record
   pass/fail + IRQ count + IOMMU `STATUS`. (Prior data: default 0x3 = PASS,
   0x2 = FAIL/walker-disabled. `0x1` is **untested** and is the value the comment
   actually intends.)
3. Confirm whether *any* AUTO_GATING write is needed at all, or whether the
   reset `0x3` default suffices once the unrelated bugs (TASK_DMA prefetch fault,
   domain reuse) are fixed — current evidence (RKNPU works on 0x3) suggests the
   write may be **unnecessary**.

Until that test runs, the only defensible upstream actions are: **(i) drop the
write**, or **(ii) ship it gated-to-v1 as an RMW that clears bit 1 / preserves
bit 0 (= 0x1 on reset), with a corrected comment** — never the current `0x2`.
