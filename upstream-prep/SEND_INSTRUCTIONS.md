# Sending the v2 series

## Ready to send

The 4-patch v2 series + cover letter is at:
`/home/dio/Downloads/claude_npu/rock3b-npu-project/upstream-prep/v2-patches/`

All patches:
- Built against v6.19-rc5 mainline
- Pass `scripts/checkpatch.pl --strict` with NO warnings or errors
- Driver builds cleanly with arm64 cross-compiler
- DTB builds cleanly
- dtbs_check passes

## Pre-flight checklist

1. **git send-email configured?** Test with:
   ```
   git send-email --dry-run --to=yourself@example.com upstream-prep/v2-patches/*.patch
   ```
   If unconfigured, set up `~/.gitconfig` with:
   ```
   [sendemail]
       smtpserver = smtp.gmail.com
       smtpserverport = 587
       smtpencryption = tls
       smtpuser = midgy971@gmail.com
   ```
   And use a Gmail app-password for authentication.

2. **Final review of cover letter**: open
   `upstream-prep/v2-patches/v2-0000-cover-letter.patch` and read through the
   subject + body. The investigation status note in particular should match
   what you want to communicate to maintainers.

3. **Optional**: do a final test apply on the live board's kernel tree to be
   extra confident. Patches are already verified on the compile server's
   v6.19-rc5 clean tree though, so this is belt-and-suspenders.

## Send command

From your workstation, in any directory:

```bash
cd /home/dio/Downloads/claude_npu/rock3b-npu-project/upstream-prep/v2-patches

git send-email \
    --to="Tomeu Vizoso <tomeu@tomeuvizoso.net>" \
    --to="Oded Gabbay <ogabbay@kernel.org>" \
    --cc="Rob Herring <robh@kernel.org>" \
    --cc="Krzysztof Kozlowski <krzk+dt@kernel.org>" \
    --cc="Conor Dooley <conor+dt@kernel.org>" \
    --cc="Heiko Stuebner <heiko@sntech.de>" \
    --cc="dri-devel@lists.freedesktop.org" \
    --cc="devicetree@vger.kernel.org" \
    --cc="linux-arm-kernel@lists.infradead.org" \
    --cc="linux-rockchip@lists.infradead.org" \
    --cc="linux-kernel@vger.kernel.org" \
    v2-*.patch
```

`git send-email` will preview each message and prompt before sending. Read
the previews carefully — the cover letter goes out first, then each patch
threaded as a reply.

## After sending

- Watch [lore.kernel.org/dri-devel/](https://lore.kernel.org/dri-devel/)
  and [lore.kernel.org/linux-rockchip/](https://lore.kernel.org/linux-rockchip/)
  for replies (mirrored within minutes).
- Address any review comments by sending a v3 (typically wait a week for
  feedback before respinning unless the issue is obvious).
- If Tomeu accepts, the series will land in drm-misc-next, then upstream.

## Compile server tree state

Branch `rocket-rk3568-v2` lives in `/root/rocket-upstream/mainline/` on the
compile server at `<TAILSCALE-IP>` (Proxmox LXC 100, accessed via Proxmox
host `<COMPILE-SERVER-IP>`). Commits:

```
4adc26cac arm64: dts: rockchip: rk3568-rock-3b: Enable NPU
648194af9 arm64: dts: rockchip: rk356x: Add NPU and its IOMMU
7c0470557 dt-bindings: npu: rockchip,rk3588-rknn-core: Add RK3568 support
898c1d2de accel: rocket: Add support for Rockchip RK3568
```

If you need to amend in response to feedback, work in that tree, then
re-run `git format-patch --cover-letter -v 3 -o /root/rocket-upstream/v3-patches/`.
