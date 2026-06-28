# RK3568 Mali-G52 (Panfrost) – Kernel 6.19 Integration Notes

## 1. GPU IP
- ARM Mali-G52 MC2 (Bifrost)
- DRM driver: drivers/gpu/drm/panfrost
- Firmware: none (fully open-source)

## 2. Required Kernel Config
CONFIG_DRM=y
CONFIG_DRM_PANFROST=y
CONFIG_PM=y
CONFIG_PM_RUNTIME=y
CONFIG_IOMMU_SUPPORT=y
CONFIG_ROCKCHIP_IOMMU=y

## 3. Power / Clock Sequence
1. Enable power domain PD_GPU
2. Deassert SRST_GPU
3. Enable aclk_gpu
4. Enable hclk_gpu
5. Enable clk_gpu_core
6. Map MMIO
7. Register DRM device

## 4. Device Tree Binding Notes
- compatible = "arm,mali-g52"
- clocks must be ordered (aclk, hclk, core)
- power-domains mandatory on RK3568

## 5. Common Failure Modes
- GPU hangs → missing hclk_gpu
- panfrost probe fails → power-domain not enabled
- GPU resets under load → incorrect PLL parent

