/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_CORE_H__
#define __ROCKET_CORE_H__

#include <drm/gpu_scheduler.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex_types.h>
#include <linux/reset.h>

#include "rocket_registers.h"

struct rocket_core;

/**
 * struct rocket_soc_data - per-SoC configuration data
 * @dma_bits: Physical address width reachable by the NPU's AXI bus.
 *            RK3568: 32 (32-bit AXI bus — GEM pages and page-table walker
 *            must reside within the first 4 GB of RAM), RK3588: 40.
 * @noc_init: optional callback to de-idle the NPU NOC bus at probe time.
 *            Required on RK3568 where the NOC must be explicitly un-idled
 *            before the NPU can be accessed.
 * @pc_data_amount_scale: divisor used to convert regcmd entry count to the
 *            PC_REGISTER_AMOUNTS hardware field value.  RK3568 counts one
 *            64-bit regcmd word per unit (scale = 1); RK3588 counts two
 *            64-bit words (one 128-bit pair) per unit (scale = 2).  Using
 *            the wrong value causes the PC to stop reading the regcmd early
 *            and never reach the final broadcast OP_EN that starts the
 *            CNA/CORE/DPU engines.
 */
struct rocket_soc_data {
	unsigned int dma_bits;
	int (*noc_init)(struct rocket_core *core);
	unsigned int pc_data_amount_scale;
};

#define rocket_pc_readl(core, reg) \
	readl((core)->pc_iomem + (REG_PC_##reg))
#define rocket_pc_writel(core, reg, value) \
	writel(value, (core)->pc_iomem + (REG_PC_##reg))

#define rocket_cna_readl(core, reg) \
	readl((core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS)
#define rocket_cna_writel(core, reg, value) \
	writel(value, (core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS)

#define rocket_core_readl(core, reg) \
	readl((core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS)
#define rocket_core_writel(core, reg, value) \
	writel(value, (core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS)

/* Forward declaration for attached_domain field below. */
struct rocket_iommu_domain;

struct rocket_core {
	struct device *dev;
	struct rocket_device *rdev;
	unsigned int index;
	const struct rocket_soc_data *soc_data;

	int irq;
	void __iomem *pc_iomem;
	void __iomem *cna_iomem;
	void __iomem *core_iomem;
	struct clk_bulk_data clks[4];
	struct reset_control_bulk_data resets[2];

	struct iommu_group *iommu_group;
	struct device *iommu_dev; /* IOMMU platform device, for rk_iommu_trigger_enable/disable */
	/* Domain currently attached to iommu_group (kref held). NULL if none. */
	struct rocket_iommu_domain *attached_domain;

	struct mutex job_lock;
	struct rocket_job *in_flight_job;

	spinlock_t fence_lock;

	struct {
		struct workqueue_struct *wq;
		struct work_struct work;
		atomic_t pending;
	} reset;

	struct drm_gpu_scheduler sched;
	u64 fence_context;
	u64 emit_seqno;
};

int rocket_core_init(struct rocket_core *core);
void rocket_core_fini(struct rocket_core *core);
void rocket_core_reset(struct rocket_core *core);

#endif
