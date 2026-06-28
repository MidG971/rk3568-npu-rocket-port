# RK3568 NPU — Mainline Linux bring-up (Radxa ROCK 3B)

Work toward running the **RK3568 NPU** (1 TOPS NVDLA-derived IP) on a **mainline** Linux
stack — both the vendor `rknpu` driver ported to a recent kernel, and the upstream DRM
**`rocket`** accel driver + **Mesa/Teflon** userspace — on the Radxa ROCK 3B.

This repository is the engineering record of that effort: driver experiments, device-tree
work, kernel/Mesa patches, debugging tools, serial logs, and a detailed findings knowledge
base.

> **Scope note:** This is a curated, de-identified snapshot. Copyrighted vendor material
> (the RK3568 TRM, the RKNN SDK, BSP images) and private collaborator correspondence have
> been **excluded** on purpose — see [`REPO-SCOPE.md`](REPO-SCOPE.md). Network identifiers
> (Tailscale IPs, the board MAC, work-context logs) have been redacted; LAN IPs and the
> public default board login (`radxa`/`radxa`) are kept so the scripts still read sensibly.

## Status

**Two tracks:**

1. **Vendor `rknpu` driver on mainline 6.19 — WORKING.** Loads on 6.19.0-rc5; IOMMU mode
   functional (YOLOv5s ≈ 57 ms, MobileNetV1 ≈ 8 ms, correct outputs). Required fixes:
   SCMI two-step rate change (PVTPLL init), PMU NoC de-idle, `always_on` NPU power domain,
   IOMMU v1 `GFP_DMA32` page-table fix, and the `RK_MMU_AUTO_GATING` fix.

2. **Upstream `rocket` accel driver on RK3568 — RFC v3 sent.** The NPU **probes and submits
   cleanly on stock mainline** via a DT-only power-domain bring-up plus rocket robustness
   fixes. Two walls remained on the compute path, and both are now characterized:
   - **Executer engage** (the NPU's per-unit pipeline never armed) — root cause is
     HW-internal init that the vendor performs; solved *in practice* with a "BSP primer"
     (run one vendor job, then hand the powered, engaged NPU to `rocket`).
   - **Per-channel requant** (int8 conv output collapses to zero-point / "degenerate") —
     **root cause found:** Mesa rocket emits no per-channel requantization (SDP `BS_MUL`
     bypassed, CNA CVT scale hardcoded, coefficient buffer = biases only). The vendor
     coefficient-buffer record format has been **decoded** and the fix is specified.

The most current status and next steps live in **[`PLAN.md`](PLAN.md)**,
**[`for_review/NEXT-SESSION-PLAN.md`](for_review/NEXT-SESSION-PLAN.md)**, and the
**[`memory/`](memory/)** knowledge base (start at `memory/MEMORY.md`).

## What's the actual bug? (latest finding)

For an int8 conv, the requant `out = (acc + bias) * (w_scale·in_scale/out_scale)` must be
applied. Mesa rocket bypasses it for multi-channel-input convs, so the output rails to the
zero-point. The vendor streams per-channel `{bias, mult, shift}` from a coefficient surface
at `DPU_RDMA.RDMA_BS_BASE_ADDR`, laid out as **8 bytes/channel**, interleaved 8 channels per
64-byte record: `[8×int32 bias | 8×int16 shift | 8×int16 mult]`. This was confirmed two ways
(the captured buffer's bytes *and* the register field widths). The fix is to build that
surface in `rkt_coefs.c` and flip `BS_MUL_SRC`/`BS_TRUNCATE_SRC` in `rkt_regcmd.c`. See
`memory/coeff-buffer-format-decoded-2026-06-27.md`.

## Repository layout

| Path | What |
|------|------|
| `PLAN.md`, `PLAN_2026-05-20.md` | Living plans for the rocket bring-up |
| `CLAUDE.md` | Operational context / running session log (build commands, config, history) |
| `docs/` | Investigation write-ups, API-porting notes (5.10 → 6.19), patch history |
| `memory/` | Findings knowledge base — one fact per file, indexed by `MEMORY.md` |
| `patches/`, `mesa-patches/` | Kernel and Mesa patch series |
| `for_review/` | Patch sets prepared for review, analysis docs, test fixtures, driver snapshots |
| `upstream-prep/` | Submission prep: cover letters, checklists, research reports |
| `dts/`, `*.dts*`, `*.dtsi` | Device-tree sources for the NPU node |
| `tools/`, `test_c/` | Small C probes (register/clock/PMU/version tests) |
| `npu-prime/` | The "BSP primer" service that engages the NPU before handing it to `rocket` |
| `rocket_*.c`, `rkt_*` | Dated driver experiment snapshots (the investigation trail) |
| `patch_*.py` | Live-patching helpers used during debugging |
| `logs/` | Serial/boot capture logs |
| `rocket_replay.c`, `*.sh` | Replay tool and run/deploy scripts |

## Quick start (board scripts)

The board uses a dynamic LAN IP; scripts discover it (you must set your own board's MAC in
`find-rock3b.sh` — it's redacted to `<BOARD-MAC>` here):

```bash
./find-rock3b.sh         # discover board IP by MAC, writes ~/.rock3b_ip
./rock3b-ssh.sh "uname -a"
```

Boot entries (extlinux) used during development: `l1` = mainline + vendor `rknpu` (IOMMU),
`l3` = mainline + `rocket`, `l8` = `rocket` with the BSP-primer auto-run at boot.

## Licensing

Mixed, per component: kernel patches/driver work follow the Linux kernel (**GPL-2.0**);
Mesa patches follow Mesa (**MIT**). Original scripts, notes, and docs here are provided
as-is; add a top-level license of your choice before publishing.

## Upstream references

- [Rocket NPU driver](https://docs.kernel.org/accel/rocket/index.html)
- [RKNPU2 SDK](https://github.com/rockchip-linux/rknpu2) · [RKNN Toolkit 2](https://github.com/rockchip-linux/rknn-toolkit2)
- RK3568 TRM Part 1 (public): rockchip opensource mirror
