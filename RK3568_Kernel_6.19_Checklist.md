# RK3568 GPU/NPU – Linux Kernel 6.19 Checklist

## API Changes to Watch
- devm_reset_control_get_optional_exclusive()
- pm_runtime_resume_and_get()
- clk_bulk APIs preferred

## DTS Validation
- dtbs_check
- YAML bindings for panfrost already upstream

## Recommended Debug
echo 0x1f > /sys/module/drm/parameters/debug
echo on > /sys/kernel/debug/clk/clk_summary

## Performance
- Enable OPP tables
- Ensure IOMMU is enabled
