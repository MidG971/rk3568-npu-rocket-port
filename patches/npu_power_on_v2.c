/*
 * NPU Power-On Code v2 - Add to rknpu_drv.c after line 55
 *
 * Changes from v1:
 * - Added PMU_VOL_GATE_SFTCON (0xa8) to enable VD_NPU voltage domain
 * - Proper sequencing: voltage domain -> power gate -> bus de-idle
 *
 * To apply: Insert this code block in rknpu_drv.c after line 55,
 * replacing the existing rknpu_pmu_power_on function if present.
 */

#ifdef FPGA_PLATFORM
#include <linux/io.h>

#define PMU_BASE                0xfdd90000
#define PMU_PWR_GATE_SFTCON     0xa0
#define PMU_BUS_IDLE_SFTCON0    0x50
#define PMU_BUS_IDLE_ST         0x68
#define PMU_PWR_DWN_ST          0x98
#define PMU_VOL_GATE_SFTCON     0xa8  /* Voltage domain control */

#define NPU_PWR_BIT             BIT(1)
#define NPU_IDLE_BIT            BIT(2)
#define NPU_VOL_BIT             BIT(1)  /* VD_NPU enable bit */

static void __iomem *pmu_base;

static int rknpu_pmu_power_on(struct device *dev)
{
    u32 val;
    int timeout = 1000;

    if (!pmu_base) {
        pmu_base = ioremap(PMU_BASE, 0x1000);
        if (!pmu_base) {
            dev_err(dev, "Failed to map PMU registers\n");
            return -ENOMEM;
        }
    }

    /* Step 1: Enable NPU voltage domain (VD_NPU) */
    val = readl(pmu_base + PMU_VOL_GATE_SFTCON);
    dev_info(dev, "VOL_GATE_SFTCON before: 0x%x\n", val);
    if (!(val & NPU_VOL_BIT)) {
        /* VD_NPU not enabled, enable it now */
        writel((NPU_VOL_BIT << 16) | NPU_VOL_BIT, pmu_base + PMU_VOL_GATE_SFTCON);
        udelay(10);  /* Small delay for voltage to stabilize */
        val = readl(pmu_base + PMU_VOL_GATE_SFTCON);
        dev_info(dev, "VOL_GATE_SFTCON after enable: 0x%x\n", val);
    }

    /* Step 2: Check if already powered on */
    val = readl(pmu_base + PMU_PWR_DWN_ST);
    if (!(val & NPU_PWR_BIT)) {
        dev_info(dev, "NPU already powered on (PWR_DWN_ST=0x%x)\n", val);
    } else {
        /* Power on NPU domain */
        writel((NPU_PWR_BIT << 16) | 0, pmu_base + PMU_PWR_GATE_SFTCON);

        while (timeout--) {
            val = readl(pmu_base + PMU_PWR_DWN_ST);
            if (!(val & NPU_PWR_BIT))
                break;
            udelay(1);
        }
        if (timeout <= 0) {
            dev_err(dev, "NPU power on timeout\n");
            return -ETIMEDOUT;
        }
        dev_info(dev, "NPU powered on (PWR_DWN_ST=0x%x)\n", val);
    }

    /* Step 3: De-idle NPU bus */
    writel((NPU_IDLE_BIT << 16) | 0, pmu_base + PMU_BUS_IDLE_SFTCON0);

    timeout = 1000;
    while (timeout--) {
        val = readl(pmu_base + PMU_BUS_IDLE_ST);
        if (!(val & NPU_IDLE_BIT))
            break;
        udelay(1);
    }
    if (timeout <= 0) {
        dev_err(dev, "NPU bus de-idle timeout\n");
        return -ETIMEDOUT;
    }
    dev_info(dev, "NPU bus de-idled (BUS_IDLE_ST=0x%x)\n", val);

    return 0;
}
#endif
