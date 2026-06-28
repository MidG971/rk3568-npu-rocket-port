# Patch Submission History

## PI6C20100 Clock Fix

### Problem

The Radxa ROCK 3B uses a PI6C20100 PCIe reference clock buffer (100 MHz).
It was incorrectly modeled as a fixed regulator (`vcc3v3_pi6c_03`), with no clock output.

### Correct Approach

Model PI6C20100 as `gated-fixed-clock` with `vdd-supply` referencing the regulator.
Pattern from `rk3588-rock-5-itx.dts`.

### Patch History

| Version | Date | Changes | Lore URL |
|---------|------|---------|----------|
| v1 | 2026-02-13 | Initial: phy-supply on pcie30phy | https://lore.kernel.org/linux-rockchip/20260213151452.535527-1-midgy971@gmail.com/ |
| v2 | 2026-03-04 | Drop phy-supply; use gated-fixed-clock + wire ref clock | https://lore.kernel.org/linux-rockchip/20260304132957.684616-1-midgy971@gmail.com/ |
| v3 | 2026-03-19 | Fix AI attribution (Assisted-by); add CLK_PCIE30X2_PIPE_DFT | — |
| v4 | 2026-03-20 | Add Reviewed-by: Shawn Lin | — |
| v5 | 2026-03-20 | Fix From: line to match Signed-off-by (Heiko's request) | — |

**v5 message ID**: `20260320094441.128263-1-midgy971@gmail.com`
**Status**: Sent to linux-rockchip, awaiting Heiko Stuebner to apply.

### Reviewer Feedback Summary

**Jonas Karlman (v1 NAK)**:
- NAK'd phy-supply approach entirely
- PI6C20100 is a clock generator, not a power supply
- Should be modeled as `gated-fixed-clock` in DTS
- Mainline U-Boot with `rock-3b-rk3568_defconfig` fixes both ethernet ports and PCIe

**Shawn Lin (v2 review)**:
- Fix AI attribution: use `Assisted-by: Claude:claude-3-opus` (NOT `Signed-off-by`)
- Add missing pipe clock `CLK_PCIE30X2_PIPE_DFT` to pcie3x2 clocks
- Reference: David's patch for the base DTS adding pipe clock

**Heiko Stuebner (v4 review)**:
- Fix From: line to match Signed-off-by (submitter name inconsistency)

**Note on pipe clock**: `CLK_PCIE30X2_PIPE_DFT` is defined in `rk3568-cru.h` but was not
registered in the CRU driver in the tested kernel. Pipe clock works once the CRU driver
registers it alongside David's DTS patch.

### v5 Patch Content

```
+	/* PI6C20100 PCIe reference clock buffer (100MHz) */
+	pcie30_refclk: pcie-clock-generator {
+		compatible = "gated-fixed-clock";
+		#clock-cells = <0>;
+		clock-frequency = <100000000>;
+		clock-output-names = "pcie30_refclk";
+		vdd-supply = <&vcc3v3_pi6c_03>;
+	};

 &pcie3x2 {
+	clocks = <&cru ACLK_PCIE30X2_MST>, <&cru ACLK_PCIE30X2_SLV>,
+		 <&cru ACLK_PCIE30X2_DBI>, <&cru PCLK_PCIE30X2>,
+		 <&cru CLK_PCIE30X2_AUX_NDFT>,
+		 <&cru CLK_PCIE30X2_PIPE_DFT>,
+		 <&pcie30_refclk>;
+	clock-names = "aclk_mst", "aclk_slv",
+		      "aclk_dbi", "pclk", "aux",
+		      "pipe", "ref";
```

## Patch Submission Guidelines

- SOB order: Co-developed-by + their SOB first, submitter's SOB LAST
- AI assistance: use `Assisted-by: Claude:claude-3-opus` (NOT Signed-off-by)
- Submitter: `Midgy BALON <midgy971@gmail.com>`
- Mailing lists: `linux-rockchip@lists.infradead.org`, `linux-arm-kernel@lists.infradead.org`, `devicetree@vger.kernel.org`
- CC: `heiko@sntech.de`, `jonas@kwiboo.se`

## Future Upstream Work

### NPU Driver

BSP RKNPU driver **cannot be submitted upstream**:
- Requires proprietary RKNN SDK userspace (must be open-source for accel/DRM)
- Uses DRM `/dev/dri/card*` instead of accel `/dev/accel/accel*`
- Contains BSP-specific hacks

**Correct path**: Add RK3568 support to the Rocket driver (`accel/rocket`)
- Rocket merged in 6.18 for RK3588 (Tomeu Vizoso, Collabora)
- RK3568 NPU: same NVDLA-derived IP, 1 core, 0.8 TOPS
- Open-source Mesa "Rocket" Gallium driver for userspace

### pm-domains Fix

The RK3568 NPU power domain needs a proper fix in `drivers/pmdomain/rockchip/pm-domains.c`.
Current workaround: `always_on` domain that bypasses power management.
Proper fix: make genpd correctly perform the SCMI+PVTPLL+PMU de-idle sequence
when powering on the NPU domain.
