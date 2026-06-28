#!/usr/bin/env python3
"""Patch rknpu_drv.c to manually power on NPU via PMU registers."""

import sys

filepath = sys.argv[1] if len(sys.argv) > 1 else 'drivers/rknpu/rknpu_drv.c'

with open(filepath, 'r') as f:
    content = f.read()

# Add includes and defines after linux/pm_runtime.h
include_add = """#include <linux/io.h>
#include <linux/delay.h>

/* RK3568 PMU registers for manual NPU power domain control */
#define RK3568_PMU_BASE        0xfdd90000
#define RK3568_PMU_PWR_GATE    0xa0
#define RK3568_PMU_PWR_STATUS  0x98
#define RK3568_PMU_BUS_REQ     0x50
#define RK3568_PMU_BUS_IDLE    0x68
#define RK3568_PMU_BUS_ACK     0x60
#define RK3568_NPU_PWR_BIT     BIT(1)
#define RK3568_NPU_REQ_BIT     BIT(2)
"""

content = content.replace(
    '#include <linux/pm_runtime.h>\n',
    '#include <linux/pm_runtime.h>\n' + include_add
)

# Add manual power-on function before rknpu_power_on
power_fn = """
/*
 * Manually power on NPU domain via direct PMU register access.
 * This bypasses the genpd framework which fails on mainline for RK3568 NPU.
 */
static int rknpu_manual_power_on(struct device *dev)
{
	void __iomem *pmu;
	u32 val;
	int timeout;

	pmu = ioremap(RK3568_PMU_BASE, 0x1000);
	if (!pmu) {
		dev_err(dev, "failed to map PMU registers\\n");
		return -ENOMEM;
	}

	/* Check if NPU is already powered on */
	val = readl(pmu + RK3568_PMU_PWR_STATUS);
	if (!(val & RK3568_NPU_PWR_BIT)) {
		dev_info(dev, "NPU power domain already on\\n");
		goto deidle;
	}

	/* Power on NPU: write mask bit[17] + clear power bit[1] */
	writel(RK3568_NPU_PWR_BIT << 16, pmu + RK3568_PMU_PWR_GATE);
	wmb();

	/* Wait for power status to show ON */
	timeout = 1000;
	while (timeout-- > 0) {
		val = readl(pmu + RK3568_PMU_PWR_STATUS);
		if (!(val & RK3568_NPU_PWR_BIT))
			break;
		udelay(10);
	}
	if (timeout <= 0) {
		dev_err(dev, "NPU power-on timeout, status=0x%08x\\n", val);
		iounmap(pmu);
		return -ETIMEDOUT;
	}
	dev_info(dev, "NPU power domain enabled (status=0x%08x)\\n", val);

deidle:
	/* De-idle NPU bus: write mask bit[18] + clear req bit[2] */
	writel(RK3568_NPU_REQ_BIT << 16, pmu + RK3568_PMU_BUS_REQ);
	wmb();
	udelay(100);

	val = readl(pmu + RK3568_PMU_BUS_ACK);
	dev_info(dev, "NPU bus ack after de-idle: 0x%08x\\n", val);

	iounmap(pmu);
	return 0;
}

"""

content = content.replace(
    'static int rknpu_power_on(struct rknpu_device *rknpu_dev)\n',
    power_fn + 'static int rknpu_power_on(struct rknpu_device *rknpu_dev)\n'
)

# Add manual power-on call before pm_runtime_enable in probe
content = content.replace(
    '\tpm_runtime_enable(dev);\n',
    '\t/* Manually power on NPU domain (mainline genpd broken for RK3568 NPU) */\n'
    '\tret = rknpu_manual_power_on(dev);\n'
    '\tif (ret)\n'
    '\t\tdev_warn(dev, "manual NPU power-on failed: %d, continuing anyway\\n", ret);\n\n'
    '\tpm_runtime_enable(dev);\n'
)

with open(filepath, 'w') as f:
    f.write(content)

print('Patch applied successfully')
