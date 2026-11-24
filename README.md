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

1. **Vendor `rknpu` driver on mainline — WORKING.** Ported to mainline **v7.4-rc4**
   (board boot entry `l12` on eMMC bullseye; **`l13` = Debian 12 bookworm on the NVMe
   loop-image is now the default boot**, bullseye kept as fallback); IOMMU mode functional (YOLOv5s ≈ 57 ms, MobileNetV1
   ≈ 8 ms, correct outputs). Required fixes: SCMI two-step rate change (PVTPLL init),
   PMU NoC de-idle, NPU power-domain regulator wiring, IOMMU v1 `GFP_DMA32` page-table
   fix, and the `RK_MMU_AUTO_GATING` bit-31 fix (now upstream as `8d4346ecd495`).

2. **Upstream `rocket` accel driver on RK3568 — one patch IN mainline; series send-ready,
   gated on the compute wall.** The NPU **probes, powers and submits cleanly on stock
   mainline v7.4-rc4** (re-baselined 2026-11-23: gate unchanged by the rc3→rc4 rocket
   hardening delta). The **`pmdomain: rockchip: Add a regulator to the RK3568 NPU
   power domain` patch is IN mainline** (`c8e1c83f9ad7`, v7.4-rc3+). The driver+DT series
   (v4, 8 patches) is rebased onto v7.4-rc3 and **self-contained** — the other former
   dependencies (per-device IOMMU ops `414cb6f3ac62`, AUTO_GATING bit-31 `8d4346ecd495`)
   are also in v7.4-rc3 — and held send-ready pending the compute wall below. Compute
   path, characterized:
   - **Compute gate (the primary wall).** Every unit engages, but the CNA never issues its
     weight-fetch DMA (`wt_rd=0` vs the vendor's `0x1900`), so the MAC array starves and the
     output stays at the zero-point. The full software-visible surface — every `writel`,
     payload replay, clocks/power/reset/IOMMU, coherency, cold PD power-cycle with a
     kiln-style NIU reset edge — has been matched to the vendor or tested negative, so the
     gate is **below the register interface**: a per-power-session hardware state that a
     *working vendor job* arms (BSP-primer) and no register write re-arms. Escalated to
     Rockchip on linux-rockchip (lore thread `20260721230746.175786`, follow-up
     `20261120112754.1400099`) and discussed in the public RK3576 sibling thread
     (github.com/gahingwoo/linux-rk3576-npu issue #1).
   - **Per-channel requant — FIXED in Mesa (this repo's `mesa-patches/`).** The vendor
     coefficient-buffer record format is decoded (`[8×int32 bias | 8×int16 shift |
     8×int16 mult]` per 64 B, 8 channels per record; Q14 multiplier, max 16384) and mesa
     now emits it by default, with `BS_CFG=0x158` matching the working vendor regcmd
     byte-exactly; validated offline against the vendor captures. End-to-end output
     correctness still waits on the kernel compute gate.
   - **Board QoL:** Ethernet jack LEDs restored on mainline (amber = link, green =
     activity, HW-offloaded to the RTL8211F) — DT patch sent to the lists
     (`[PATCH v2] ... rk3568-rock-3b: describe the Ethernet PHY LEDs`,
     Msg-Id `20261120123250.1417061-1`).
   - **Mesa Draft MR opened**: [!46552](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/46552)
     — `Draft: rocket: Add RK3568 NPU support for Teflon`, branch `rk3568-teflon`
     @ `8eee49103808` (rebased onto main `bf593eb0` 2026-11-20, conflict-free, compile-checked on board;
     commit carries `Assisted-by: LLM` per Mesa's AI-disclosure rule). Held in
     Draft until the kernel compute gate clears and a conv completes end-to-end
     on hardware.

The most current status and next steps live in **[`PLAN.md`](PLAN.md)** and the
**[`memory/`](memory/)** knowledge base (start at `memory/MEMORY.md`).

## What's the actual bug? (latest finding)

For an int8 conv, the requant `out = (acc + bias) * (w_scale·in_scale/out_scale)` must be
applied. Mesa rocket bypassed it for multi-channel-input convs, so the output rails to the
zero-point. The vendor streams per-channel `{bias, shift, mult}` from a coefficient surface
at `DPU_RDMA.RDMA_BS_BASE_ADDR`, laid out as **8 bytes/channel**, interleaved 8 channels per
64-byte record: `[8×int32 bias | 8×int16 shift | 8×int16 mult]` (mult is Q14, max 16384;
the absolute scale stays in DPU `OUT_CVT`). This was confirmed two ways (the captured
buffer's bytes *and* the register field widths). **Fixed** in `mesa-patches/`
(`mesa-coefs.patch`, `mesa-regcmd.patch`, 2026-11-20): mesa now builds that surface by
default and emits `BS_CFG=0x158`, byte-identical to the working vendor regcmd; validated
offline against the vendor captures (see `mesa-patches/README-2026-11-20-vendorfmt.md`).
End-to-end correctness additionally needs the kernel-side compute gate above to clear.

## Repository layout

| Path | What |
|------|------|
| `PLAN.md` | Living plan for the rocket bring-up (current truth + dated session log) |
| `CLAUDE.md` | Operational context / running session log (build commands, config, history) |
| `README.md`, `REPO-SCOPE.md` | This guide + the curation/scope notes |
| `driver-snapshots/` | Dated `rocket_*.c` driver experiment snapshots (the investigation trail) + early reference C |
| `debug-scripts/` | `patch_*.py` live-patching helpers and register/serial probe scripts |
| `notes/` | Session logs, analysis notes, and earlier plans (`SESSION_*`, `PLAN_2026-05-20.md`, …) |
| `scripts/` | Deploy / run / setup shell scripts (board-access `find-rock3b.sh` + `rock3b-ssh.sh` stay at root) |
| `logs/` | Serial / boot capture logs |
| `dts/` | Device-tree sources + the NPU binding (`*.dts`, `*.dtsi`, `*.yaml`) |
| `docs/` | Investigation write-ups, API-porting notes (5.10 → 6.19), patch history |
| `memory/` | Findings knowledge base — one fact per file, indexed by `MEMORY.md` |
| `patches/`, `mesa-patches/` | Kernel and Mesa patch series |
| `for_review/` | Patch sets prepared for review, analysis docs, test fixtures |
| `npu-prime/`, `npu-probe/` | The "BSP primer" service + read-only register probe tool |
| `tools/`, `test_c/` | Small C probes (register/clock/PMU/version tests) |

## Quick start (board scripts)

The board uses a dynamic LAN IP; scripts discover it (you must set your own board's MAC in
`find-rock3b.sh` — it's redacted to `<BOARD-MAC>` here):

```bash
./find-rock3b.sh         # discover board IP by MAC, writes ~/.rock3b_ip
./rock3b-ssh.sh "uname -a"
```

Boot entries (extlinux) used during development: `l1`/`l3`/`l8` = 6.19-era test kernels
(vendor `rknpu` IOMMU / `rocket` / BSP-primer), `l10`/`l11` = mainline v7.4-rc3 + RK3568 NPU
port, `l12` = **mainline v7.4-rc4 on eMMC bullseye (fallback)**, `l13` = **v7.4-rc4 on
Debian 12 bookworm (NVMe loop-image `/mnt/nvme/bookworm.img`, custom loop-root initrd) —
DEFAULT boot since 2026-11-24**.

## Licensing

This repository is licensed **GPL-2.0** (see [`LICENSE`](LICENSE)) — appropriate because
the bulk is kernel-derived driver/DTS work, which inherits the Linux kernel's GPL-2.0.
The **Mesa** patches under `mesa-patches/` (and any Mesa source snippets) remain under
**MIT** as marked in their own headers, per Mesa's licensing. Original scripts, notes, and
docs are provided under GPL-2.0 as well.

## Upstream references

- [Rocket NPU driver](https://docs.kernel.org/accel/rocket/index.html)
- [RKNPU2 SDK](https://github.com/rockchip-linux/rknpu2) · [RKNN Toolkit 2](https://github.com/rockchip-linux/rknn-toolkit2)
- RK3568 TRM Part 1 (public): rockchip opensource mirror
- Compute-gate escalation on linux-rockchip:
  [thread](https://lore.kernel.org/all/20260721230746.175786-1-midgy971@gmail.com/)
  + [follow-up 2026-11-20](https://lore.kernel.org/all/20261120112754.1400099-1-midgy971@gmail.com/)
- RK3576 sibling collaboration thread: [gahingwoo/linux-rk3576-npu#1](https://github.com/gahingwoo/linux-rk3576-npu/issues/1)
- Ethernet PHY LED DT patch on the lists:
  [v1](https://lore.kernel.org/all/20261120113555.1402132-1-midgy971@gmail.com/) ·
  [v2](https://lore.kernel.org/all/20261120123250.1417061-1-midgy971@gmail.com/)
