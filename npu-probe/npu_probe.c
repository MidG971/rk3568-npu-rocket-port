// SPDX-License-Identifier: GPL-2.0
/*
 * npu_probe — READ-ONLY snapshot of the RK3568 NPU register state.
 *
 * Purpose: on a PRIMED board (NPU powered + engaged, no active job), dump the
 * CNA/CMAC/CORE/DPU/DPU_RDMA status + the weight/data DMA perf counters so we can
 * compare RK3568's weight-fetch state against gahingwoo's RK3576 numbers.
 *
 * SAFETY: this ONLY does readl() via ioremap — the kernel-side path that rocket.ko
 * and the primer already use safely. It never writes, and it deliberately maps only
 * 0x0000..0x8fff so 0xf008 (GLOBAL_OP_EN — writing it hangs the NoC) is never touched.
 * (Userspace /dev/mem reads of the >=0x8000 perf region are what hang the board; a
 * kernel ioremap read is the proven-safe way — see project history.)
 *
 * Load with `insmod npu_probe.ko`; the snapshot is printed at init; `rmmod` after.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>

#define NPU_PHYS_BASE 0xfde40000UL
#define NPU_MAP_SIZE  0x9000U   /* 0x0000..0x8fff — excludes 0xf008 by construction */

struct npu_reg { u32 off; const char *name; };

/*
 * Curated weight-fetch-relevant set. Offsets are unit_base + field:
 *   S_STATUS = base+0x000 (bit16 = EXECUTER engaged), S_POINTER = base+0x004
 *   (0x1000e = armed+engaged vs 0x0e = cold), OP_EN = base+0x008/0x00c.
 * Perf/DMA counters at 0x80xx are the weight-read / data-read / data-write meters
 * (the "wt_rd / dt_rd / dt_wr" gahingwoo watches).
 */
static const struct npu_reg regs[] = {
	{ 0x0014, "PC_STATUS" },
	{ 0x0018, "PC_INT_STATUS" },
	/* CNA (convolution buffer / feature) */
	{ 0x1000, "CNA_S_STATUS" },
	{ 0x1004, "CNA_S_POINTER" },
	{ 0x1008, "CNA_OP_EN" },
	{ 0x1040, "CNA_CBUF_CON0" },
	{ 0x1044, "CNA_CBUF_CON1" },
	{ 0x1140, "CNA_DCOMP_AMOUNT0" },
	/* CMAC (MAC array) */
	{ 0x2000, "CMAC_S_STATUS" },
	{ 0x2004, "CMAC_S_POINTER" },
	{ 0x200c, "CMAC_OP_EN" },
	/* CORE / ACCU (accumulator) */
	{ 0x3000, "CORE_S_STATUS" },
	{ 0x3004, "CORE_S_POINTER" },
	{ 0x3008, "CORE_OP_EN" },
	{ 0x300c, "CORE_MAC_GATING" },
	/* DPU (output / SDP) */
	{ 0x4000, "DPU_S_STATUS" },
	{ 0x4004, "DPU_S_POINTER" },
	{ 0x400c, "DPU_OP_EN" },
	/* DPU_RDMA (bias / BS stream) */
	{ 0x5000, "RDMA_S_STATUS" },
	{ 0x5004, "RDMA_S_POINTER" },
	{ 0x500c, "RDMA_OP_EN" },
	/* perf / DMA counters — weight-read, data-read, data-write meters */
	{ 0x8010, "PERF_8010_amount_top" },
	{ 0x8034, "PERF_8034" },
	{ 0x8038, "PERF_8038" },
	{ 0x803c, "PERF_803c" },
};

static int __init npu_probe_init(void)
{
	void __iomem *base;
	unsigned int i;

	base = ioremap(NPU_PHYS_BASE, NPU_MAP_SIZE);
	if (!base) {
		pr_err("npu_probe: ioremap(0x%lx) failed\n", NPU_PHYS_BASE);
		return -ENOMEM;
	}

	pr_info("npu_probe: ===== RK3568 NPU register snapshot (READ-ONLY) =====\n");
	for (i = 0; i < ARRAY_SIZE(regs); i++)
		pr_info("npu_probe: %-22s [0x%04x] = 0x%08x\n",
			regs[i].name, regs[i].off, readl(base + regs[i].off));
	pr_info("npu_probe: ===== end snapshot =====\n");

	iounmap(base);
	return 0;
}

static void __exit npu_probe_exit(void) { }

module_init(npu_probe_init);
module_exit(npu_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only RK3568 NPU register/perf-counter snapshot");
