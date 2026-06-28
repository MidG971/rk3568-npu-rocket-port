// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "rocket_core.h"
#include "rocket_job.h"

/* 2026-05-20: aligned to BSP rknpu clock-names order. SCMI clock FIRST
 * because clk_bulk_prepare_enable iterates in this order — matters for
 * NPU power-up sequence (BSP enables SCMI/PVTPLL clock before bus clocks). */
static const char * const rocket_clk_names[] = { "scmi_clk", "clk", "aclk", "hclk" };

int rocket_core_init(struct rocket_core *core)
{
	struct device *dev = core->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u32 version;
	int err = 0;

	core->resets[0].id = "srst_a";
	core->resets[1].id = "srst_h";
	err = devm_reset_control_bulk_get_exclusive(&pdev->dev, ARRAY_SIZE(core->resets),
						    core->resets);
	if (err)
		return dev_err_probe(dev, err, "failed to get resets for core %d\n", core->index);

	for (int i = 0; i < ARRAY_SIZE(rocket_clk_names); i++)
		core->clks[i].id = rocket_clk_names[i];

	err = devm_clk_bulk_get(dev, ARRAY_SIZE(core->clks), core->clks);
	if (err)
		return dev_err_probe(dev, err, "failed to get clocks for core %d\n", core->index);

	core->pc_iomem = devm_platform_ioremap_resource_byname(pdev, "pc");
	if (IS_ERR(core->pc_iomem)) {
		dev_err(dev, "couldn't find PC registers %ld\n", PTR_ERR(core->pc_iomem));
		return PTR_ERR(core->pc_iomem);
	}

	core->cna_iomem = devm_platform_ioremap_resource_byname(pdev, "cna");
	if (IS_ERR(core->cna_iomem)) {
		dev_err(dev, "couldn't find CNA registers %ld\n", PTR_ERR(core->cna_iomem));
		return PTR_ERR(core->cna_iomem);
	}

	core->core_iomem = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(core->core_iomem)) {
		dev_err(dev, "couldn't find CORE registers %ld\n", PTR_ERR(core->core_iomem));
		return PTR_ERR(core->core_iomem);
	}

	dma_set_max_seg_size(dev, UINT_MAX);

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(core->soc_data->dma_bits));
	if (err)
		return err;

	core->iommu_group = iommu_group_get(dev);

	/*
	 * OPP framework setup: tell OPP which clock and regulator to coordinate,
	 * then load the OPP table from DT. dev_pm_opp_set_rate() can then atomically
	 * set both clock rate and matching regulator voltage. This is the lightweight
	 * equivalent of RKNPU's devfreq init — no governor, just OPP-coordinated
	 * power transitions to satisfy the NPU's expectation of a "valid OPP" state.
	 */
	err = devm_pm_opp_set_clkname(dev, "scmi_clk");  /* was "npu" — renamed to match BSP DT */
	if (err)
		dev_warn(dev, "rocket: opp_set_clkname(scmi_clk) failed: %d\n", err);
	{
		const char * const supplies[] = { "npu" };
		err = devm_pm_opp_set_regulators(dev, supplies);
		if (err)
			dev_warn(dev, "rocket: opp_set_regulators(npu) failed: %d\n", err);
	}
	err = devm_pm_opp_of_add_table(dev);
	if (err)
		dev_warn(dev, "rocket: opp_of_add_table failed: %d\n", err);
	else
		dev_info(dev, "rocket: OPP table loaded\n");

	err = rocket_job_init(core);
	if (err)
		return err;

	pm_runtime_use_autosuspend(dev);

	/*
	 * As this NPU will be most often used as part of a media pipeline that
	 * ends presenting in a display, choose 50 ms (~3 frames at 60Hz) as an
	 * autosuspend delay as that will keep the device powered up while the
	 * pipeline is running.
	 */
	pm_runtime_set_autosuspend_delay(dev, 3600000);  /* 2026-05-20 test: 1 hour — effectively disable autosuspend to test if cycle sets CNA bit 2 */

	pm_runtime_enable(dev);

	err = pm_runtime_resume_and_get(dev);
	if (err) {
		rocket_job_fini(core);
		return err;
	}

	version = rocket_pc_readl(core, VERSION);
	version += rocket_pc_readl(core, VERSION_NUM) & 0xffff;

	/* 2026-05-20: PROBE_CNA — read CNA state at probe time BEFORE first
	 * autosuspend cycle. Compares with PRE_OP readback (which fires after
	 * at least one autosuspend cycle has gated/resumed clocks). If PROBE_CNA
	 * S_STATUS=0x0 but PRE_OP S_STATUS=0x4 → autosuspend/clock-gate cycle
	 * sets the sticky bit. If both =0x4 → set by clock-enable / noc_init
	 * itself, present from the first runtime_resume. */
	if (core->cna_iomem)
		dev_err(dev, "PROBE_CNA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CBUF_CON0=%08x DCOMP_CTRL=%08x",
			readl(core->cna_iomem + 0x00),
			readl(core->cna_iomem + 0x04),
			readl(core->cna_iomem + 0x08),
			readl(core->cna_iomem + 0x40),
			readl(core->cna_iomem + 0x100));
	if (core->core_iomem)
		dev_err(dev, "PROBE_CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
			readl(core->core_iomem + 0x00),
			readl(core->core_iomem + 0x04),
			readl(core->core_iomem + 0x08));

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	dev_info(dev, "Rockchip NPU core %d version: %d\n", core->index, version);


	return 0;
}

void rocket_core_fini(struct rocket_core *core)
{
	pm_runtime_dont_use_autosuspend(core->dev);
	pm_runtime_disable(core->dev);
	iommu_group_put(core->iommu_group);
	core->iommu_group = NULL;
	rocket_job_fini(core);
}

void rocket_core_reset(struct rocket_core *core)
{
	reset_control_bulk_assert(ARRAY_SIZE(core->resets), core->resets);

	udelay(10);

	reset_control_bulk_deassert(ARRAY_SIZE(core->resets), core->resets);

	/*
	 * Hardware reset leaves the NPU bus master in idle state; re-run
	 * SoC-specific bus init so the IOMMU stall can complete for the
	 * next job's domain switch.
	 */
	if (core->soc_data->noc_init)
		core->soc_data->noc_init(core);
}
