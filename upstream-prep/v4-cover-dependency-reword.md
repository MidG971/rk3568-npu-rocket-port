# v4 cover-letter "Dependency" section — reworded (v1/v2 consistency)

Blocker A from the v4 review: the v3/v4 cover says "Rockchip confirmed the NPU MMU
is v2" but patch 8's DTS declares `compatible = "rockchip,iommu"` (the v1 ops).
This reword removes that contradiction — it states what the DTS actually does (use
the v1 ops for the 32-bit DTE) and turns the v2 point into an open question.

REPLACE the current cover "Dependency." paragraph with the text below when v4 is
re-rolled. (Keep the [1] Simon Xue lore reference.)

---

Dependency. The NPU MMU's DTE (top-level page table) must stay below 4 GiB on
this block; on boards with more than 4 GiB of RAM the default page-table
allocations can land above it and the NPU's first translation faults
(DMA_READ_ERROR). The mainline rockchip-iommu driver already has a 32-bit-DTE ops
set -- the "rockchip,iommu" (v1) ops, which allocate page tables with GFP_DMA32 --
while the SoC's other IOMMUs use the 40-bit "rockchip,rk3568-iommu" (v2) ops.
This series therefore declares the NPU MMU with the v1 compatible to obtain the
sub-4 GiB DTE, and it works on the board.

That coexistence only holds once the driver carries per-device ops: with the
current global rk_ops, a v1 NPU MMU trips WARN_ON(rk_ops != ops) against the
SoC's v2 instances. So the series is developed on top of Simon Xue's
"iommu/rockchip: Drop global rk_ops in favor of per-device ops" [1]; without it
the NPU IOMMU cannot select the 32-bit-DTE ops.

Rockchip has noted the NPU MMU block is v2-class. We use the v1 ops here only
because they are what currently provides the required 32-bit DTE; whether a v2
ops variant carrying GFP_DMA32 (or another DTE-capping mechanism) would be the
better long-term shape is an open question -- guidance from the iommu maintainers
and Rockchip welcome.

---

Note: this keeps the DTS as-is (rockchip,iommu / v1). If Chaoyi or the iommu
maintainers prefer the DTS to use rockchip,rk3568-iommu (v2), that needs a new v2
ops variant with GFP_DMA32 in rockchip-iommu.c (the existing v2 ops use gfp=0 /
40-bit, which gives the >4 GiB DTE fault) -- a follow-up beyond this series.
