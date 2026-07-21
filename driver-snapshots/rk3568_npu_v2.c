// rk3568_npu.c – minimal skeleton

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

struct rk3568_npu {
    struct clk *aclk;
    struct clk *hclk;
    struct reset_control *rst;
};

static int rk3568_npu_probe(struct platform_device *pdev)
{
    struct rk3568_npu *npu;

    npu = devm_kzalloc(&pdev->dev, sizeof(*npu), GFP_KERNEL);
    if (!npu)
        return -ENOMEM;

    npu->aclk = devm_clk_get(&pdev->dev, "aclk");
    npu->hclk = devm_clk_get(&pdev->dev, "hclk");
    npu->rst  = devm_reset_control_get(&pdev->dev, NULL);

    clk_prepare_enable(npu->aclk);
    clk_prepare_enable(npu->hclk);
    reset_control_deassert(npu->rst);

    pm_runtime_enable(&pdev->dev);
    platform_set_drvdata(pdev, npu);
    return 0;
}

static int rk3568_npu_remove(struct platform_device *pdev)
{
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
    },
};

module_platform_driver(rk3568_npu_driver);
MODULE_LICENSE("GPL");
