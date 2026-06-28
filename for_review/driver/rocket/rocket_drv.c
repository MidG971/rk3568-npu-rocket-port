// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/rocket_accel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "rocket_drv.h"
#include "rocket_gem.h"
#include "rocket_job.h"

/* From drivers/iommu/rockchip-iommu.c */
extern int rk_iommu_trigger_enable(struct device *iommu_dev);
extern void rk_iommu_trigger_disable(struct device *iommu_dev);

/*
 * Facade device, used to expose a single DRM device to userspace, that
 * schedules jobs to any RKNN cores in the system.
 */
static struct platform_device *drm_dev;
static struct rocket_device *rdev;

/*
 * RK3568 NOC de-idle: the NPU bus must be explicitly un-idled before the
 * NPU hardware can be accessed.  The RK3568 PMU provides BUS_IDLE_SFTCON0
 * (offset 0x50) and NOC_AUTO_CON0 (offset 0x70) for this purpose.  Refer
 * to the RK3568 TRM section "PMU" for the write-mask protocol used by
 * these registers (bits [31:16] are write-enable for bits [15:0]).
 */
static int rk3568_noc_init(struct rocket_core *core)
{
	struct regmap *pmu;
	unsigned int val;
	int ret;

	pmu = syscon_regmap_lookup_by_phandle(core->dev->of_node, "rockchip,pmu");
	if (IS_ERR(pmu))
		return dev_err_probe(core->dev, PTR_ERR(pmu),
				     "failed to get PMU regmap\n");

	/*
	 * Open NPU power gate (PWR_GATE_SFTCON bit1 = 0 → NPU powered on).
	 * The genpd always_on domain callback is a no-op, so the hardware
	 * remains power-gated at boot unless we open it explicitly here.
	 * Write-mask protocol: bits [31:16] = write-enable for bits [15:0].
	 */
	regmap_write(pmu, 0xa0, BIT(1 + 16));

	/* Disable NPU NOC auto-idle so the bus stays awake */
	regmap_write(pmu, 0x70, BIT(2 + 16));

	/* Request NPU bus de-idle (bit 2 = 0 → active) */
	regmap_write(pmu, 0x50, BIT(2 + 16));

	/* Wait for NPU bus to become active (BUS_IDLE_ST bit 2 = 0) */
	ret = regmap_read_poll_timeout(pmu, 0x60, val, !(val & BIT(2)), 10, 1000);
	if (ret)
		dev_err(core->dev, "timeout waiting for NPU bus de-idle\n");

	return ret;
}

static const struct rocket_soc_data rk3568_soc_data = {
	.dma_bits = 32,
	.noc_init = rk3568_noc_init,
	.pc_data_amount_scale = 1, /* RK3568: 64-bit DMA reads, scale=1 */
};

static const struct rocket_soc_data rk3588_soc_data = {
	.dma_bits = 40,
	.pc_data_amount_scale = 2,
};

/* Forward declaration — full definition follows rocket_remove(). */
static const struct of_device_id dt_match[];

static void
rocket_iommu_domain_destroy(struct kref *kref)
{
	struct rocket_iommu_domain *domain = container_of(kref, struct rocket_iommu_domain, kref);

	iommu_domain_free(domain->domain);
	domain->domain = NULL;
	kfree(domain);
}

static struct rocket_iommu_domain*
rocket_iommu_domain_create(struct device *dev)
{
	struct rocket_iommu_domain *domain = kmalloc(sizeof(*domain), GFP_KERNEL);
	void *err;

	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->domain = iommu_paging_domain_alloc(dev);
	if (IS_ERR(domain->domain)) {
		err = ERR_CAST(domain->domain);
		kfree(domain);
		return err;
	}
	kref_init(&domain->kref);

	return domain;
}

struct rocket_iommu_domain *
rocket_iommu_domain_get(struct rocket_file_priv *rocket_priv)
{
	kref_get(&rocket_priv->domain->kref);
	return rocket_priv->domain;
}

void
rocket_iommu_domain_put(struct rocket_iommu_domain *domain)
{
	kref_put(&domain->kref, rocket_iommu_domain_destroy);
}

static int
rocket_open(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_device *rdev = to_rocket_device(dev);
	struct rocket_file_priv *rocket_priv;
	u64 start, end;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	rocket_priv = kzalloc(sizeof(*rocket_priv), GFP_KERNEL);
	if (!rocket_priv) {
		ret = -ENOMEM;
		goto err_put_mod;
	}

	rocket_priv->rdev = rdev;
	rocket_priv->domain = rocket_iommu_domain_create(rdev->cores[0].dev);
	if (IS_ERR(rocket_priv->domain)) {
		ret = PTR_ERR(rocket_priv->domain);
		goto err_free;
	}

	file->driver_priv = rocket_priv;

	start = rocket_priv->domain->domain->geometry.aperture_start;
	end = rocket_priv->domain->domain->geometry.aperture_end;
	drm_mm_init(&rocket_priv->mm, start, end - start + 1);
	mutex_init(&rocket_priv->mm_lock);

	ret = rocket_job_open(rocket_priv);
	if (ret)
		goto err_mm_takedown;

	return 0;

err_mm_takedown:
	mutex_destroy(&rocket_priv->mm_lock);
	drm_mm_takedown(&rocket_priv->mm);
	rocket_iommu_domain_put(rocket_priv->domain);
err_free:
	kfree(rocket_priv);
err_put_mod:
	module_put(THIS_MODULE);
	return ret;
}

static void
rocket_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;
	unsigned int i;

	/* Drain all pending jobs from this file before touching the domain. */
	rocket_job_close(rocket_priv);

	/*
	 * postclose: detach domain from the IOMMU group while it is still
	 * alive.  Without this, iommu_domain_free() is called while
	 * group->domain still points to the (about-to-be-freed) paging
	 * domain, leaving a dangling pointer that causes -EBUSY on the
	 * next open+attach — even across a modprobe -r / modprobe cycle.
	 */
	for (i = 0; i < rdev->num_cores; i++) {
		if (rdev->cores[i].attached_domain == rocket_priv->domain) {
			iommu_detach_group(rocket_priv->domain->domain,
					   rdev->cores[i].iommu_group);
			rocket_iommu_domain_put(rdev->cores[i].attached_domain);
			rdev->cores[i].attached_domain = NULL;
		}
	}

	mutex_destroy(&rocket_priv->mm_lock);
	/*
	 * DRM calls postclose() BEFORE drm_gem_release(), so BO drm_mm nodes
	 * are still present here (BOs haven't been freed yet). The IOMMU
	 * domain is being destroyed anyway, so forcibly remove all remaining
	 * VA-space nodes to prevent drm_mm_takedown() WARN corruption.
	 */
	{
		struct drm_mm_node *node, *tmp;

		drm_mm_for_each_node_safe(node, tmp, &rocket_priv->mm)
			drm_mm_remove_node(node);
	}
	drm_mm_takedown(&rocket_priv->mm);
	rocket_iommu_domain_put(rocket_priv->domain);
	kfree(rocket_priv);
	module_put(THIS_MODULE);
}

static const struct drm_ioctl_desc rocket_drm_driver_ioctls[] = {
#define ROCKET_IOCTL(n, func) \
	DRM_IOCTL_DEF_DRV(ROCKET_##n, rocket_ioctl_##func, 0)

	ROCKET_IOCTL(CREATE_BO, create_bo),
	ROCKET_IOCTL(SUBMIT, submit),
	ROCKET_IOCTL(PREP_BO, prep_bo),
	ROCKET_IOCTL(FINI_BO, fini_bo),
};

DEFINE_DRM_ACCEL_FOPS(rocket_accel_driver_fops);

/*
 * Rocket driver version:
 * - 1.0 - initial interface
 */
static const struct drm_driver rocket_drm_driver = {
	.driver_features	= DRIVER_COMPUTE_ACCEL | DRIVER_GEM,
	.open			= rocket_open,
	.postclose		= rocket_postclose,
	.gem_create_object	= rocket_gem_create_object,
	.ioctls			= rocket_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(rocket_drm_driver_ioctls),
	.fops			= &rocket_accel_driver_fops,
	.name			= "rocket",
	.desc			= "rocket DRM",
};

static int rocket_probe(struct platform_device *pdev)
{
	if (rdev == NULL) {
		/* First core probing, initialize DRM device. */
		rdev = rocket_device_init(drm_dev, &rocket_drm_driver, dt_match);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to initialize rocket device\n");
			return PTR_ERR(rdev);
		}
	}

	unsigned int core = rdev->num_cores;

	dev_set_drvdata(&pdev->dev, rdev);

	rdev->cores[core].rdev = rdev;
	rdev->cores[core].dev = &pdev->dev;
	rdev->cores[core].index = core;
	rdev->cores[core].soc_data = of_device_get_match_data(&pdev->dev);

	/*
	 * Look up the IOMMU platform device from the "iommus" DT phandle so
	 * we can call rk_iommu_trigger_enable/disable at the right time.  On
	 * RK3568 the IOMMU device is PM-suspended when iommu_attach_group() is
	 * called (pm_runtime_get_if_in_use returns 0), so the IOMMU hardware
	 * enable is deferred (hw_needs_enable=true).  trigger_enable() must be
	 * called after the NPU bus is de-idled and iommu_attach_group() has
	 * set up the domain, to actually enable the IOMMU hardware.
	 */
	{
		struct device_node *iommu_np =
			of_parse_phandle(pdev->dev.of_node, "iommus", 0);
		if (iommu_np) {
			struct platform_device *iommu_pdev =
				of_find_device_by_node(iommu_np);
			of_node_put(iommu_np);
			if (iommu_pdev) {
				rdev->cores[core].iommu_dev = &iommu_pdev->dev;
				put_device(&iommu_pdev->dev);
			}
		}
		dev_info(&pdev->dev, "iommu_dev = %p\n", rdev->cores[core].iommu_dev);
	}

	rdev->num_cores++;

	return rocket_core_init(&rdev->cores[core]);
}

static void rocket_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	for (unsigned int core = 0; core < rdev->num_cores; core++) {
		if (rdev->cores[core].dev == dev) {
			rocket_core_fini(&rdev->cores[core]);
			rdev->num_cores--;
			break;
		}
	}

	if (rdev->num_cores == 0) {
		/* Last core removed, deinitialize DRM device. */
		rocket_device_fini(rdev);
		rdev = NULL;
	}
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "rockchip,rk3568-rknn-core", .data = &rk3568_soc_data },
	{ .compatible = "rockchip,rk3588-rknn-core", .data = &rk3588_soc_data },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static int find_core_for_dev(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);

	for (unsigned int core = 0; core < rdev->num_cores; core++) {
		if (dev == rdev->cores[core].dev)
			return core;
	}

	return -1;
}

static int rocket_device_runtime_resume(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core = find_core_for_dev(dev);
	int err = 0;

	if (core < 0)
		return -ENODEV;

	err = clk_bulk_prepare_enable(ARRAY_SIZE(rdev->cores[core].clks), rdev->cores[core].clks);
	if (err) {
		dev_err(dev, "failed to enable (%d) clocks for core %d\n", err, core);
		return err;
	}

	/*
	 * On RK3568 the NPU NOC bus goes idle whenever the clocks are gated.
	 * Re-run the SoC-specific bus init (PMU de-idle) each time clocks are
	 * re-enabled so the IOMMU stall mechanism can complete before the
	 * first DMA domain switch of each job.
	 */
	if (rdev->cores[core].soc_data->noc_init) {
		/* Force hardware reset of NPU/IOMMU to unlock control registers on RK3568 */
		reset_control_bulk_assert(ARRAY_SIZE(rdev->cores[core].resets),
					  rdev->cores[core].resets);
		udelay(10);
		reset_control_bulk_deassert(ARRAY_SIZE(rdev->cores[core].resets),
					    rdev->cores[core].resets);

		/*
		 * On RK3568, the NPU hardware reset resets the internal clock-source
		 * MUX back to CRU, bypassing PVTPLL.  SCMI clk_enable is a no-op in
		 * TF-A; only clk_set_rate() triggers clk_npu_set_rate() in TF-A which
		 * re-configures PVTPLL and switches the MUX back to it.  Without this
		 * two-step the NPU compute core runs with no clock and all jobs time out
		 * with CNA/CORE S_STATUS=0 even though the IOMMU is correctly enabled.
		 * clks[2] = npu = SCMI CLK_NPU (see rocket_core.c clock names array).
		 */
		clk_set_rate(rdev->cores[core].clks[2].clk, 600000000UL);
		clk_set_rate(rdev->cores[core].clks[2].clk, 1000000000UL);

		err = rdev->cores[core].soc_data->noc_init(&rdev->cores[core]);
		if (err) {
			clk_bulk_disable_unprepare(ARRAY_SIZE(rdev->cores[core].clks),
						   rdev->cores[core].clks);
			return err;
		}

		/*
		 * Hardware reset cleared IOMMU paging state. Detach any domain
		 * that was attached before the clock-gate / PM suspend so the
		 * upcoming iommu_attach_group() in rocket_job_run does not
		 * return -EBUSY.
		 */
		if (rdev->cores[core].attached_domain) {
			iommu_detach_group(rdev->cores[core].attached_domain->domain,
					   rdev->cores[core].iommu_group);
			rocket_iommu_domain_put(rdev->cores[core].attached_domain);
			rdev->cores[core].attached_domain = NULL;
		}
	}

	return 0;
}

static int rocket_device_runtime_suspend(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core = find_core_for_dev(dev);

	if (core < 0)
		return -ENODEV;

	if (!rocket_job_is_idle(&rdev->cores[core]))
		return -EBUSY;

	/*
	 * Disable IOMMU hardware before gating clocks.  The IOMMU shares the
	 * NPU bus clocks; leaving paging enabled while clocks are off corrupts
	 * the IOMMU state.  rk_iommu_trigger_enable() will re-enable it on the
	 * next runtime_resume → job_run → iommu_attach_group sequence.
	 */
	if (rdev->cores[core].iommu_dev)
		rk_iommu_trigger_disable(rdev->cores[core].iommu_dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(rdev->cores[core].clks), rdev->cores[core].clks);

	return 0;
}

EXPORT_GPL_DEV_PM_OPS(rocket_pm_ops) = {
	RUNTIME_PM_OPS(rocket_device_runtime_suspend, rocket_device_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver rocket_driver = {
	.probe = rocket_probe,
	.remove = rocket_remove,
	.driver	 = {
		.name = "rocket",
		.pm = pm_ptr(&rocket_pm_ops),
		.of_match_table = dt_match,
	},
};

static int __init rocket_register(void)
{
	drm_dev = platform_device_register_simple("rknn", -1, NULL, 0);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	return platform_driver_register(&rocket_driver);
}

static void __exit rocket_unregister(void)
{
	platform_driver_unregister(&rocket_driver);

	platform_device_unregister(drm_dev);
}

module_init(rocket_register);
module_exit(rocket_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DRM driver for the Rockchip NPU IP");
MODULE_AUTHOR("Tomeu Vizoso");
