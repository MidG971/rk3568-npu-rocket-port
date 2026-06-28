# v3 binding patch — fixes for GATE 6 + Krzysztof's review

## Bug 1: missing `$ref:` (Krzysztof: "I don't think you tested this")
In the v2 yaml the `rockchip,pmu` property line is:
```yaml
  rockchip,pmu:
    : /schemas/types.yaml#/definitions/phandle      # <-- BROKEN: no key
```
Fix:
```yaml
  rockchip,pmu:
    $ref: /schemas/types.yaml#/definitions/phandle
    description:
      Phandle to the PMU syscon. The RK3568 NPU's NOC bus-idle and power
      gating are controlled through PMU registers; this points to that syscon.
```

## Bug 2: driver-centric commit message (Krzysztof: "explain the hardware, not drivers")
v2 body said "...is consumed by the driver to issue PMU register writes...".
v3 commit message (hardware-altitude):

    dt-bindings: npu: rockchip,rk3588-rknn-core: Add RK3568

    The RK3568 carries a single core of the same NVDLA-derived NPU IP as the
    RK3588. Add its compatible.

    On RK3568 the NPU's NOC bus-idle and power-gating are controlled through
    the system PMU rather than a dedicated register block, so add a
    rockchip,pmu phandle to that syscon. The RK3568 NPU has no dedicated SRAM
    rail, so sram-supply is required only on RK3588.

    Signed-off-by: Midgy BALON <midgy971@gmail.com>

## GATE 6 must run before send (the gate we failed on v2):
  yamllint Documentation/devicetree/bindings/npu/rockchip,rk3588-rknn-core.yaml
  make ARCH=arm64 dt_binding_check \
    DT_SCHEMA_FILES=Documentation/devicetree/bindings/npu/rockchip,rk3588-rknn-core.yaml
Pass = zero errors mentioning the file.
