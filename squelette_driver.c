// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RK3568 NPU driver (skeleton)
 *
 * Author: Midgy BALON <midgy971@gmail.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/iommu.h>

struct rk3568_npu {
	void __iomem *regs;
	struct clk_bulk_data clks[3];
	struct reset_control *rst;
};

static int rk3568_npu_runtime_resume(struct device *dev)
{
	struct rk3568_npu *npu = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(npu->clks), npu->clks);
	if (ret)
		return ret;

	ret = reset_control_deassert(npu->rst);
	if (ret)
		clk_bulk_disable_unprepare(ARRAY_SIZE(npu->clks), npu->clks);

	return ret;
}

static int rk3568_npu_runtime_suspend(struct device *dev)
{
	struct rk3568_npu *npu = dev_get_drvdata(dev);

	reset_control_assert(npu->rst);
	clk_bulk_disable_unprepare(ARRAY_SIZE(npu->clks), npu->clks);

	return 0;
}

static const struct dev_pm_ops rk3568_npu_pm_ops = {
	SET_RUNTIME_PM_OPS(rk3568_npu_runtime_suspend,
			   rk3568_npu_runtime_resume, NULL)
};

static int rk3568_npu_probe(struct platform_device *pdev)
{
	struct rk3568_npu *npu;
	struct resource *res;
	int ret;

	npu = devm_kzalloc(&pdev->dev, sizeof(*npu), GFP_KERNEL);
	if (!npu)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	npu->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(npu->regs))
		return PTR_ERR(npu->regs);

	npu->clks[0].id = "aclk";
	npu->clks[1].id = "hclk";
	npu->clks[2].id = "core";

	ret = devm_clk_bulk_get(&pdev->dev,
				ARRAY_SIZE(npu->clks), npu->clks);
	if (ret)
		return ret;

	npu->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(npu->rst))
		return PTR_ERR(npu->rst);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	platform_set_drvdata(pdev, npu);

	dev_info(&pdev->dev, "RK3568 NPU initialized (skeleton)\n");
	return 0;
}

static int rk3568_npu_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id rk3568_npu_of_match[] = {
	{ .compatible = "rockchip,rk3568-npu" },
	{}
};
MODULE_DEVICE_TABLE(of, rk3568_npu_of_match);

static struct platform_driver rk3568_npu_driver = {
	.probe  = rk3568_npu_probe,
	.remove = rk3568_npu_remove,
	.driver = {
		.name = "rk3568-npu",
		.of_match_table = rk3568_npu_of_match,
		.pm = &rk3568_npu_pm_ops,
	},
};

module_platform_driver(rk3568_npu_driver);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Rockchip RK3568 NPU driver (skeleton)");
MODULE_LICENSE("GPL");
