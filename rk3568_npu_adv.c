// rk3568_npu_adv.c – extended skeleton (Kernel 6.19)

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>

struct rk3568_npu {
    void __iomem *regs;
    struct clk *aclk;
    struct clk *hclk;
    struct clk *core;
    struct reset_control *rst;
    int irq;
};

static irqreturn_t rk3568_npu_irq(int irq, void *data)
{
    // Placeholder: clear interrupt status
    return IRQ_HANDLED;
}

static int rk3568_npu_probe(struct platform_device *pdev)
{
    struct rk3568_npu *npu;
    struct resource *res;

    npu = devm_kzalloc(&pdev->dev, sizeof(*npu), GFP_KERNEL);
    if (!npu)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    npu->regs = devm_ioremap_resource(&pdev->dev, res);

    npu->irq = platform_get_irq(pdev, 0);
    devm_request_irq(&pdev->dev, npu->irq, rk3568_npu_irq, 0,
                     dev_name(&pdev->dev), npu);

    npu->aclk = devm_clk_get(&pdev->dev, "aclk");
    npu->hclk = devm_clk_get(&pdev->dev, "hclk");
    npu->core = devm_clk_get(&pdev->dev, "core");
    npu->rst  = devm_reset_control_get(&pdev->dev, NULL);

    clk_prepare_enable(npu->aclk);
    clk_prepare_enable(npu->hclk);
    clk_prepare_enable(npu->core);
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
