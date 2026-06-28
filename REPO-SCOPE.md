# Repository scope — what's included and what was excluded

This repo is a **curated, de-identified snapshot** of the RK3568 NPU bring-up project.
The following was **deliberately excluded** or **redacted** before assembling it.

## Excluded files/directories (not present here)

| Category | Examples | Why |
|----------|----------|-----|
| **Rockchip TRM** | `Rockchip_RK3568_TRM_*.pdf`, `trm/`, `trm_txt.txt`, `RK3568_GPU_NPU_TRM_Extraction*.md`, `docs/npu-trm-{decoded,registers}.md` | Copyrighted Rockchip documentation and verbatim register extracts |
| **Vendor SDK** | `rknn-convert/` (6.7 GB) | Rockchip RKNN toolkit — redistribute from upstream, not here |
| **Board/firmware images** | `IMG_ISO/`, `rock-3b_bsp.img`, `spi-backup-*.bin`, `bsp-image.sha512`, `uboot/` (firmware blob) | Large binaries / vendor BSP firmware |
| **BSP recon (verbatim vendor)** | `bsp_files/`, `bsp-userspace-recon/`, `radxa_bsp_rk3568.c`, `rockchip_bsp_rk3568.c`, vendor regcmd/wtrace dumps, `*-bsp.rknn` | Derived from vendor source/binaries |
| **Private correspondence** | every `*-reply*`, `gahingwoo-*`, `chaoyi-*`, `allbilly-*`, `tomeu-*`, `jonas-*`, `diederik-*`, `for_review/from_gahingwoo/`, outreach drafts, recipient lists | Confidential collaboration with other developers |
| **Personal / unrelated** | `Certificat_*.pdf`, `AnyDesk.exe`, `sashiko_review/`, `proposal_biding_rk3568/`, the `[ssh …]`/`[shell …]` work logs | Not project material / personal / work-context |
| **Build artifacts** | `*.o`, `*.ko`, `*.dtb`, compiled test binaries, `__pycache__/` | Regenerable |

## Redactions (in otherwise-included files)

- **Tailscale IPs** (`100.64.0.0/10`) → `<BOARD-TAILSCALE-IP>` / `<COMPILE-SERVER-IP>` / `<TAILSCALE-IP>`.
- **Board MAC addresses** → `<BOARD-MAC>` (you must fill in your own in `find-rock3b.sh`).
- **SSH debug dump** (`ssh_dbg.txt`) removed entirely.

## Intentionally kept

- LAN/private IPs (e.g. `192.168.x.x`) — not sensitive.
- The public default board login `radxa`/`radxa` — documented Radxa default, needed for scripts to read sensibly.
- Author identity in upstream patches (`Midgy BALON <midgy971@gmail.com>`) — already public on the kernel/Mesa mailing lists.

## Heads-up before publishing

The `memory/` knowledge base and some `for_review/` / `upstream-prep/` analysis docs
**reference and summarize collaborators' findings** (e.g. RK3576 results shared privately),
even though the standalone correspondence files were removed. If you intend to make this
repo **public**, review those for any third-party findings you'd rather not disclose, and
add a top-level license.
