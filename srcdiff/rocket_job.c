// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_print.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/rocket_accel.h>
#include <linux/dma-resv.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iosys-map.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/mm.h>

#include "rocket_core.h"
#include "rocket_device.h"
#include "rocket_drv.h"
#include "rocket_gem.h"
#include "rocket_job.h"
#include "rocket_registers.h"

/* Bisect-regcmd module parameters: NOP regcmd entries in [nop_start, nop_end]
 * (inclusive). -1 means no NOPing. Used to find which config entry blocks
 * CNA's OP_ENABLE acceptance. */
static int nop_start = -1;
module_param(nop_start, int, 0644);
MODULE_PARM_DESC(nop_start, "First regcmd entry index to NOP (-1 = disabled)");
static int nop_end = -1;
module_param(nop_end, int, 0644);
MODULE_PARM_DESC(nop_end, "Last regcmd entry index to NOP (-1 = disabled)");

/* DCOMP_CTRL override: if >= 0, patch all regcmd entries that write to
 * 0x1100 (CNA_DCOMP_CTRL) with this value instead of mesa's (which is 0).
 * Bit 3 = WT_DEC_BYPASS — set this to disable weight decompression. */
static int dcomp_ctrl_val = -1;
module_param(dcomp_ctrl_val, int, 0644);
MODULE_PARM_DESC(dcomp_ctrl_val, "Override CNA_DCOMP_CTRL value in regcmd (-1 = disabled)");

/* clean_run: when 1, skip ALL experimental regcmd mutations (slot-7 CMAC
 * inject, OP_EN-offset redirects, BSP YOLOv5s value overlay, BSP_INJECT).
 * Submits mesa's NATIVE regcmd unchanged — correct conv2d shape + native
 * CNA OP_ENABLE at 0x1008. Use to get an attributable baseline. */
static int clean_run;
module_param(clean_run, int, 0644);
MODULE_PARM_DESC(clean_run, "Skip all experimental regcmd patches (pure mesa regcmd)");

/* desc_mode: 1 = use task-descriptor model (pp_en + TASK_DMA_BASE);
 * 0 = direct regcmd exec (no descriptor). Decoupled from clean_run so we can
 * test native regcmd + no-ping-pong + descriptor together. */
static int desc_mode = 1;
module_param(desc_mode, int, 0644);
MODULE_PARM_DESC(desc_mode, "Use task-descriptor model (1) vs direct regcmd (0)");

/* cmac_open: CPU-write CMAC OP_ENABLE (0x2008=1) before the PC OP_EN pulse.
 * Mesa's regcmd emits OP_ENABLE for CNA/ACCU/DPU/DPU_RDMA but NEVER for CMAC
 * (the MAC array), so the multiply never runs. CMAC regs are CPU-writable. */
static int cmac_open;
module_param(cmac_open, int, 0644);
MODULE_PARM_DESC(cmac_open, "CPU-assert CMAC OP_ENABLE (0x2008) before OP_EN pulse");

/* cna_sp: if >=0, CPU-write this value to CNA S_POINTER (0x1004) right before
 * the OP_EN pulse. CNA is the sole sub-unit not starting; it may need a
 * specific S_POINTER (group-select bit0 / pp_clear bits4-5) to commit its
 * config group and enter the executer. -1 = leave untouched. */
static int cna_sp = -1;
module_param(cna_sp, int, 0644);
MODULE_PARM_DESC(cna_sp, "CPU-write CNA S_POINTER value before OP_EN pulse (-1=off)");

/* conv_con1: if >=0, patch regcmd entry off=0x100c (CNA_CONV_CON1) to this
 * value. Mesa emits 0; BSP uses 0xa000 (conv-mode enable bits). CNA may not
 * start the conv with mode=0. -1 = leave mesa value. */
static int conv_con1 = -1;
module_param(conv_con1, int, 0644);
MODULE_PARM_DESC(conv_con1, "Patch CNA_CONV_CON1 (0x100c) regcmd value (-1=off)");

/* cna_last: move CNA OP_ENABLE (off=0x1008) to the END of the regcmd, after
 * the other sub-units' OP_ENABLEs. CNA is the producer; consumers (CMAC/ACCU/
 * DPU) may need to be enabled first. */
static int cna_last;
module_param(cna_last, int, 0644);
MODULE_PARM_DESC(cna_last, "Move CNA OP_ENABLE to end of regcmd (1=on)");

/* compound_open: append the TRM all-block op_en entry
 * 64'h0081_0000_007f_0008 (offset 0x0008, value 0x7f = block bitmask, bit55 =
 * "set each block op_en"). BSP uses this single broadcast instead of mesa's
 * individual per-block 0x_008=1 writes, which CNA ignores. */
static int compound_open;
module_param(compound_open, int, 0644);
MODULE_PARM_DESC(compound_open, "Append TRM compound all-block op_en entry (1=on)");

/* cbuf_con0: if >=0, in-place patch CNA_CBUF_CON0 (0x1040) value. Mesa emits
 * 0x44 (weight_bank=4 data_bank=4) computed for RK3588 12 banks; RK3568 has 8.
 * Bad CBUF alloc -> CNA DMA never starts. BSP uses 0x17 (wbank=1 dbank=7). */
static int cbuf_con0 = -1;
module_param(cbuf_con0, int, 0644);
MODULE_PARM_DESC(cbuf_con0, "In-place patch CNA_CBUF_CON0 (0x1040) value (-1=off)");

/* no_block_open: NOP every per-block OP_ENABLE regcmd entry (0x1008/2008/
 * 3008/4008/5008). BSP writes NONE of these — it relies on the CPU PC
 * OPERATION_ENABLE pulse to enable all configured blocks. Test if rocket's
 * CNA auto-enables BSP-style when mesa's explicit op_en writes are removed. */
static int no_block_open;
module_param(no_block_open, int, 0644);
MODULE_PARM_DESC(no_block_open, "NOP all per-block OP_ENABLE entries, BSP-style (1=on)");

/* bsp_cna: overlay BSP's EXACT CNA config values (from byte-exact conv2d diff
 * 2026-05-31) onto mesa's regcmd. mesa's values are wrong for RK3568:
 * line_stride 4x too big, CONV_CON4 should be 0, CONV_CON2 kernel_group wrong. */
static int bsp_cna;
module_param(bsp_cna, int, 0644);
MODULE_PARM_DESC(bsp_cna, "Overlay BSP CNA config values (1=on)");

/* desc_en_mask: value written to task descriptor d[2] (enable_mask). BSP's PC
 * likely reads this to enable blocks (CNA=0x02 CMAC=0x04 ACCU=0x08 DPU=0x10
 * DPU_RDMA=0x20; 0x3e=all five). Rocket left it 0. Test with no_block_open=1
 * to rely purely on the BSP descriptor-enable mechanism. */
static int desc_en_mask;
module_param(desc_en_mask, int, 0644);
MODULE_PARM_DESC(desc_en_mask, "Task descriptor enable_mask (d[2]) value");

/* op_en_fix (2026-06-01): clean BSP OP_ENABLE-scheme correction, runs UNDER
 * clean_run (unlike the contaminated OP_EN_OFFSET_FIX block which is !clean_run
 * gated). For our exact conv (byte-exact BSP capture):
 *   - mesa writes spurious 1 to STATUS reg 0x_008 (CNA/ACCU/DPU/DPU_RDMA).
 *   - real OPERATION_ENABLE is at 0x_00c; BSP: CMAC=1, ACCU=0, DPU=0x108,
 *     DPU_RDMA=0x27. DPU/DPU_RDMA 0x_00c already emitted by mesa as config;
 *     CMAC 0x200c is missing entirely. BSP has NO CNA enable.
 * This block: NOP mesa's 0x1008/0x3008/0x4008/0x5008; write ACCU 0x300c=0;
 * append CMAC 0x200c=1. Keeps ping-pong (run with nop=-1) like BSP.
 * Pair with bsp_cna=1 for correct config VALUES (separate concern). */
static int op_en_fix;
module_param(op_en_fix, int, 0644);
MODULE_PARM_DESC(op_en_fix, "Clean BSP OP_ENABLE scheme: 0x_00c enables, no CNA enable (1=on)");

/* pc_slave_pre (2026-06-01): replicate BSP's REG_WRITE(0x1, PC_DATA_ADDR) issued
 * before the real regcmd address (BSP "switch to slave mode"). Rocket omits it. */
static int pc_slave_pre;
module_param(pc_slave_pre, int, 0644);
MODULE_PARM_DESC(pc_slave_pre, "Pre-write 0x1 to PC BASE before regcmd addr (BSP slave-mode, 1=on)");

/* task_pp (2026-06-02): decouple TASK_CON bit12 (task_pp_en, BSP ping-pong TASK mode)
 * from desc_mode. Rocket conflated them: desc_mode set BOTH the descriptor (TASKDMA)
 * AND TASK_CON bit12. BSP RK3568 combo (bit12 SET + TASKDMA=0) was never tested.
 * -1 = legacy (follow desc_mode); 0 = force bit12 clear; 1 = force bit12 set. */
static int task_pp = -1;
module_param(task_pp, int, 0644);
MODULE_PARM_DESC(task_pp, "TASK_CON bit12 task_pp_en: -1=follow desc_mode, 0=clear, 1=set");

/* cmac_bsp (2026-06-02): inject BSP's EXACT CMAC (MAC array) setup that mesa/rocket
 * entirely omit. Full byte-exact diff: BSP writes 0x2004=0xe (CMAC S_POINTER
 * ping-pong) + 0x200c=0x1 (CMAC OP_ENABLE); rocket never touches CMAC S_POINTER and
 * cmac_open used the wrong offset (0x2008). If CMAC is the missing CBUF consumer (CNA
 * fills CBUF then hangs waiting for the MAC to drain it), this should unblock compute. */
static int cmac_bsp;
module_param(cmac_bsp, int, 0644);
MODULE_PARM_DESC(cmac_bsp, "Inject BSP CMAC setup 0x2004=0xe + 0x200c=1 (1=on)");

/* replay_bsp (2026-06-02): DECISIVE content-vs-HW test. Overwrite rocket's mesa
 * regcmd with BSP's byte-exact 133-entry command list (captured for the IDENTICAL
 * conv via conv2d-bsp.rknn on l1), substituting ONLY the 4 real IOVA fields with
 * rocket's own BO addresses (harvested from the mesa regcmd first). Reproduces BSP's
 * structure + ordering + CMAC setup + 0x_00c enable scheme simultaneously. WORKS ⇒
 * gate was regcmd structure (fix mesa). FAILS ⇒ gate is HW-state outside the list.
 * Run standalone (no bsp_cna/cmac_open). nop=-1 = ping-pong faithful; nop=0..5 =
 * ping-pong OFF variant (NOPs the 0x_004 S_POINTER writes at replay slots 0-5). */
static int replay_bsp;
module_param(replay_bsp, int, 0644);
MODULE_PARM_DESC(replay_bsp, "Overwrite regcmd with byte-exact BSP list, IOVAs swapped (1=on)");

/* From drivers/iommu/rockchip-iommu.c */
extern int rk_iommu_trigger_enable(struct device *iommu_dev);
extern int full_reset;	/* defined in rocket_drv.c — 0=off, 1=on-resume, 2=per-job */
extern void rk_iommu_flush_tlb(struct device *iommu_dev);
extern void rk_iommu_read_status(struct device *dev, u32 *dte, u32 *status, u32 *fa, u32 *rawst);
extern int rk_iommu_resume_paging(struct device *iommu_dev);


#define JOB_TIMEOUT_MS 500

static struct rocket_job *
to_rocket_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct rocket_job, base);
}

static const char *rocket_fence_get_driver_name(struct dma_fence *fence)
{
	return "rocket";
}

static const char *rocket_fence_get_timeline_name(struct dma_fence *fence)
{
	return "rockchip-npu";
}

static const struct dma_fence_ops rocket_fence_ops = {
	.get_driver_name = rocket_fence_get_driver_name,
	.get_timeline_name = rocket_fence_get_timeline_name,
};

static struct dma_fence *rocket_fence_create(struct rocket_core *core)
{
	struct dma_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	dma_fence_init(fence, &rocket_fence_ops, &core->fence_lock,
		       core->fence_context, ++core->emit_seqno);

	return fence;
}

static int
rocket_copy_tasks(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct drm_rocket_job *job,
		  struct rocket_job *rjob)
{
	int ret = 0;

	if (job->task_struct_size < sizeof(struct drm_rocket_task))
		return -EINVAL;

	rjob->task_count = job->task_count;

	if (!rjob->task_count)
		return 0;

	rjob->tasks = kvmalloc_array(job->task_count, sizeof(*rjob->tasks), GFP_KERNEL);
	if (!rjob->tasks) {
		drm_dbg(dev, "Failed to allocate task array\n");
		return -ENOMEM;
	}

	for (int i = 0; i < rjob->task_count; i++) {
		struct drm_rocket_task task = {0};

		if (copy_from_user(&task,
				   u64_to_user_ptr(job->tasks) + i * job->task_struct_size,
				   sizeof(task))) {
			drm_dbg(dev, "Failed to copy incoming tasks\n");
			ret = -EFAULT;
			goto fail;
		}

		if (task.regcmd_count == 0) {
			drm_dbg(dev, "regcmd_count field in drm_rocket_task should be > 0.\n");
			ret = -EINVAL;
			goto fail;
		}

		rjob->tasks[i].regcmd = task.regcmd;
		rjob->tasks[i].regcmd_count = task.regcmd_count;
	}

	return 0;

fail:
	kvfree(rjob->tasks);
	return ret;
}

/* Byte-exact BSP regcmd for conv2d-bsp.rknn (133 entries), captured on l1
 * (REGCMD_FULL_BSP dump). replay_bsp overwrites rocket's regcmd with these verbatim,
 * substituting ONLY the 4 real IOVA fields (0x1070 input, 0x1110 weight, 0x4020 output,
 * 0x5020 dpu_rdma-src) with rocket's own BO addresses. All other values are config and
 * identical to mesa's. Enables interleaved at 0x_00c; NO CNA enable; PC OP_EN pulsed by
 * the driver separately (not in this list). */
static const struct bsp_re { u32 off; u32 val; u8 rt; } BSP_REGCMD[] = {
	{0x1004, 0x0000000e, 0x02}, {0x2004, 0x0000000e, 0x04}, {0x3004, 0x0000000e, 0x08},
	{0x1040, 0x00000044, 0x02}, {0x4004, 0x0000000e, 0x10}, {0x5004, 0x0000000e, 0x20},
	{0x100c, 0x00000000, 0x02}, {0x1010, 0x00000070, 0x02}, {0x1014, 0x00000012, 0x02},
	{0x1018, 0x00000000, 0x02}, {0x1020, 0x00500050, 0x02}, {0x1024, 0x000f0010, 0x02},
	{0x1028, 0x00000028, 0x02}, {0x102c, 0x00000640, 0x02}, {0x1030, 0x0000c800, 0x02},
	{0x1034, 0x00000190, 0x02}, {0x1038, 0x05050080, 0x02}, {0x1040, 0x00000044, 0x02},
	{0x1044, 0x00500028, 0x02}, {0x104c, 0x0000000a, 0x02}, {0x1050, 0x00010000, 0x02},
	{0x1054, 0x00010000, 0x02}, {0x1058, 0x00010000, 0x02}, {0x105c, 0x00010000, 0x02},
	{0x1060, 0x00000000, 0x02}, {0x1064, 0x00000000, 0x02}, {0x1068, 0x00000011, 0x02},
	{0x1070, 0xffee0000, 0x02}, {0x1074, 0x00000000, 0x02}, {0x1078, 0x00171c07, 0x02},
	{0x107c, 0x00000050, 0x02}, {0x1080, 0x00001900, 0x02}, {0x1084, 0x00500050, 0x02},
	{0x1088, 0x00000010, 0x02}, {0x1100, 0x00000000, 0x02}, {0x1104, 0x00000000, 0x02},
	{0x1110, 0xfffe0000, 0x02}, {0x1114, 0x00000000, 0x02}, {0x1118, 0x00000000, 0x02},
	{0x111c, 0x00000000, 0x02}, {0x1120, 0x00000000, 0x02}, {0x1124, 0x00000000, 0x02},
	{0x1128, 0x00000000, 0x02}, {0x112c, 0x00000000, 0x02}, {0x1130, 0x00000000, 0x02},
	{0x1134, 0x00000000, 0x02}, {0x1138, 0x00000000, 0x02}, {0x113c, 0x00000000, 0x02},
	{0x1140, 0x00000000, 0x02}, {0x1144, 0x00000000, 0x02}, {0x1148, 0x00000000, 0x02},
	{0x114c, 0x00000000, 0x02}, {0x1210, 0x00000000, 0x02}, {0x1214, 0x00000000, 0x02},
	{0x1218, 0x00000000, 0x02}, {0x121c, 0x00000000, 0x02}, {0x1220, 0x00000000, 0x02},
	{0x1224, 0x00000000, 0x02}, {0x1228, 0x00000000, 0x02}, {0x122c, 0x00000000, 0x02},
	{0x1230, 0x00000000, 0x02}, {0x200c, 0x00000001, 0x04}, {0x300c, 0x00000000, 0x08},
	{0x3010, 0x00270027, 0x08}, {0x3014, 0x0000007f, 0x08}, {0x302c, 0x00000000, 0x08},
	{0x400c, 0x00000108, 0x10}, {0x4010, 0x00000000, 0x10}, {0x4014, 0x00000000, 0x10},
	{0x4020, 0xfff19000, 0x10}, {0x4024, 0x00003200, 0x10}, {0x4030, 0x00000027, 0x10},
	{0x4034, 0x00000027, 0x10}, {0x4038, 0x00000000, 0x10}, {0x403c, 0x007f007f, 0x10},
	{0x4040, 0x00000158, 0x10}, {0x4044, 0x00000000, 0x10}, {0x4048, 0x00000000, 0x10},
	{0x404c, 0x00000000, 0x10}, {0x4050, 0x00000124, 0x10}, {0x4054, 0xfffffffb, 0x10},
	{0x4060, 0x00000053, 0x10}, {0x4064, 0x00000000, 0x10}, {0x4068, 0x00000000, 0x10},
	{0x406c, 0x00000000, 0x10}, {0x4070, 0x00000383, 0x10}, {0x4074, 0x00000000, 0x10},
	{0x4078, 0x00000001, 0x10}, {0x407c, 0x00000000, 0x10}, {0x4080, 0xffffff80, 0x10},
	{0x4084, 0x00005324, 0x10}, {0x4088, 0x0000000e, 0x10}, {0x4090, 0x00000000, 0x10},
	{0x4094, 0x00000000, 0x10}, {0x4098, 0x00000000, 0x10}, {0x409c, 0x00000000, 0x10},
	{0x40a0, 0x00000000, 0x10}, {0x40a4, 0x00000000, 0x10}, {0x40a8, 0x00000000, 0x10},
	{0x40ac, 0x00000000, 0x10}, {0x40c0, 0x00006400, 0x10}, {0x40c4, 0x00000000, 0x10},
	{0x4100, 0x00000000, 0x10}, {0x4104, 0x00000000, 0x10}, {0x4108, 0x00000000, 0x10},
	{0x410c, 0x00000000, 0x10}, {0x4110, 0x00000000, 0x10}, {0x4114, 0x00000000, 0x10},
	{0x4118, 0x00000000, 0x10}, {0x411c, 0x00000000, 0x10}, {0x4120, 0x00000000, 0x10},
	{0x4124, 0x00000000, 0x10}, {0x4128, 0x00000000, 0x10}, {0x412c, 0x00000000, 0x10},
	{0x500c, 0x00000027, 0x20}, {0x5010, 0x00000027, 0x20}, {0x5014, 0x0000007f, 0x20},
	{0x5018, 0x00000000, 0x20}, {0x501c, 0x00000002, 0x20}, {0x5020, 0xfffec800, 0x20},
	{0x5024, 0x0000003f, 0x20}, {0x5028, 0x00000001, 0x20}, {0x502c, 0x00000000, 0x20},
	{0x5030, 0x00000000, 0x20}, {0x5034, 0x00000001, 0x20}, {0x5038, 0x00000000, 0x20},
	{0x503c, 0x00000000, 0x20}, {0x5040, 0x00000000, 0x20}, {0x5044, 0x00004000, 0x20},
	{0x5048, 0x00000000, 0x20}, {0x504c, 0x00000000, 0x20}, {0x5064, 0x00000000, 0x20},
	{0x5068, 0x01010101, 0x20},
};

/*
 * 2026-06-01 Track A2: full-pipeline S_STATUS sweep. Until now only CNA (0x1000)
 * and ACCU/"CORE" (0x3000) were sampled over time; CMAC/DPU/DPU_RDMA were a single
 * AT10MS snapshot. Sample EVERY sub-unit's S_STATUS (offset 0x_000) at each
 * timepoint so the FIRST stage that fails to advance is visible. Pipeline order:
 * CNA -> CMAC -> ACCU -> DPU, with DPU_RDMA feeding DPU bias/residual.
 * S_STATUS (0x_000) is HW-set internal pipeline state (executer operating/idle),
 * unlike 0x_008 which just echoes the last written run-enable value.
 * CBUF_CON0 bit13 (DATA_REUSE) is the HW "real execution" litmus (BSP=0x2017 on a
 * real run; rocket stuck at 0x44 = never executed).
 */
static void rocket_pipe_sweep(struct rocket_core *core, const char *tag)
{
	void __iomem *p = ioremap(0xfde40000, 0x10000);
	u32 cbuf;

	if (!p)
		return;
	cbuf = readl(p + 0x1040);
	dev_err(core->dev,
		"%s PIPE Sstatus: CNA=%08x CMAC=%08x ACCU=%08x DPU=%08x RDMA=%08x | CBUF0=%08x bit13=%d | run8: CNA=%x CMAC=%x ACCU=%x DPU=%x RDMA=%x",
		tag,
		readl(p + 0x1000), readl(p + 0x2000), readl(p + 0x3000),
		readl(p + 0x4000), readl(p + 0x5000),
		cbuf, !!(cbuf & (1u << 13)),
		readl(p + 0x1008), readl(p + 0x2008), readl(p + 0x3008),
		readl(p + 0x4008), readl(p + 0x5008));
	iounmap(p);
}

static void rocket_job_hw_submit(struct rocket_core *core, struct rocket_job *job)
{
	struct rocket_task *task;
	unsigned int extra_bit;
	unsigned int scale = core->soc_data->pc_data_amount_scale;

	/* Don't queue the job if a reset is in progress */
	if (atomic_read(&core->reset.pending))
		return;

	/* PRE_SUBMIT_RESET DISABLED 2026-05-19 PM: hypothesis — our reset
	 * (srst + iommu detach/attach) clears CNA's internal pipeline state
	 * that BSP RKNPU accumulates across submits. BSP doesn't reset between
	 * submits. Test if removing this lets CNA reach EXECUTER state. */
	dev_err(core->dev, "PRE_SUBMIT_RESET: skipped (test)");

	/* FIX 2026-05-18: pre-set bit 0 (POINTER_PP_EN) of DPU + DPU_RDMA S_POINTER.
	 *
	 * On RK3568, mesa's regcmd writes 0xe to 0x4004 (DPU_S_POINTER) and
	 * 0x5004 (DPU_RDMA_S_POINTER) early in the program (entries 4, 5).
	 * That value uses CNA-style PP_EN bit layout (bits 1,2,3) but DPU's
	 * S_POINTER has POINTER_PP_EN at bit 0. So mesa never asserts PP_EN
	 * on DPU; PC's subsequent broadcasts to DPU are rejected; DPU stays
	 * at HW defaults (S_STATUS=5, OP_EN=0, DST_BASE=0) for the whole job.
	 *
	 * Bisection isolated the wake to bit 0: writel(0x1, DPU_S_POINTER)
	 * before submit is sufficient. Once awake, DPU accepts mesa's later
	 * 0xe overwrite without losing the wake state. Same for DPU_RDMA.
	 *
	 * TODO: encode via proper PC_*S_POINTER field macros once we confirm
	 * the field name in rocket_registers.h (currently only CNA/CORE
	 * S_POINTER macros are defined; DPU may have its own layout). */
	{
		void __iomem *_npu = ioremap(0xfde40000, 0x10000);
		if (_npu) {
			/* Best-known S_POINTER pre-writes (2026-05-18).
			 *
			 * Bit 0 = "accept broadcasts" wake (sticky).
			 * - CNA/CORE: writel(0x1) lets PC broadcast config writes
			 *   successfully (CBUF_CON0=0x84 lands). However CNA's
			 *   OP_ENABLE broadcast (entry 129) still gets rejected.
			 *   Likely a pipeline prerequisite (DCOMP_CTRL? other regs?)
			 *   is missing — not the S_POINTER bits.
			 * - DPU/DPU_RDMA: writel(0x1) wakes them; OP_ENABLE lands.
			 *
			 * Values tested for CNA: 0 (no PC broadcast), 0xe (blocks PC),
			 * 0x1 (works for configs, not OP_ENABLE), 0x2 (blocks PC),
			 * 0x5 (works for configs, not OP_ENABLE). Bit 0 is the only
			 * known-good wake; OP_ENABLE acceptance is a separate issue.
			 */
			/* Keep our proven "bit 0 = accept-broadcasts" wake for the
			 * sub-units mesa rocket addresses. Writing BSP's value 0xe
			 * here breaks PC broadcast (bit 1 = PP_EN puts them in a
			 * waiting state that mesa regcmd never matches). */
			/* 2026-05-20: CNA_S_POINTER pre-write DROPPED.
			 * Bisect proved this CPU write sets CNA S_STATUS bit 2
			 * sticky (RESERVED_1[0]), which BSP does NOT have at PRE_OP.
			 * BSP_INJECT[4] (PC-broadcast writel(0xe, CNA_S_POINTER))
			 * lands later via the regcmd path; the CPU pre-write was
			 * an obsolete diagnostic from before BSP_INJECT was added.
			 * Test: does removing this let CNA reach EXECUTER state? */
			/* writel(0x1, _npu + 0x1004); */  /* CNA_S_POINTER — REMOVED 2026-05-20 PM (sets sticky S_STATUS bit 2) */
			/* writel(0x1, _npu + 0x3004); */  /* CORE_S_POINTER — REMOVED 2026-05-20 PM v3 (same sticky bit symptom as CNA) */
			// CPU_PREWRITES_OFF: writel(0x1, _npu + 0x4004);    /* DPU_S_POINTER */
			/* 2026-05-28 PROBE: zero DPU OP_EN before submit. If PC really
			 * broadcasts the regcmd, this should be set back to 1 by PC.
			 * If it stays 0 at AT10MS, PC isn't broadcasting at all. */
			// CPU_PREWRITES_OFF: writel(0x0, _npu + 0x4008);    /* DPU OP_EN PROBE: zero from CPU */
			dev_err(core->dev, "PROBE: zeroed DPU OP_EN, readback=0x%x", readl(_npu + 0x4008));
			/* writel(0x1, _npu + 0x5004); */  /* DPU_RDMA_S_POINTER — REMOVED 2026-05-21 (was sticky bit-2; mesa entry 5 writes 0xe which DOES wake DPU_RDMA — different bit layout from DPU?) */

			/* 2026-05-21 v3 reverted: writing BSP YOLOv5s DPU_RDMA values
			 * caused DPU_RDMA OP_ENABLE to be REJECTED (was=1, became=0).
			 * Confirmed: mesa values are right for conv2d tensor, BSP values
			 * are right for YOLOv5s. Register meanings tensor-shape-dependent.
			 * Reverted. */
			/* writel(0x13f, _npu + 0x500c); */
			/* writel(0x15, _npu + 0x5010); */
			/* writel(0xf, _npu + 0x5014); */

			/* 2026-05-21 mesa now emits CONV_CON4 via rkt_regcmd.c — CPU
			 * pre-write of 0x1018 no longer needed. Pipeline gate satisfied
			 * by mesa's RGB_BYTELENGTH emission. */
			// CPU_PREWRITES_OFF: writel(0x3de00, _npu + 0x1018);  /* CONV_CON4 BSP YOLOv5s value */
			/* 2026-05-28: BYPASS CPU writes REMOVED — caused board hang.
			 * CNA's MMIO is sensitive to mixed CPU/PC writes. Use only regcmd
			 * patching path going forward. */
			/* 2026-05-27: obsolete DPU/DPU_RDMA/CORE/CNA CPU pre-writes REMOVED.
			 * They hardcoded old low-IOVA layout (e.g., DST_BASE=0x73000).
			 * With Option B2, BOs are now at high IOVAs (0xffe80000 etc) and
			 * the regcmd carries the correct values. Pre-writes would conflict. */

			/* MYSTERY sub-unit at 0x2xxx (routing bitmask 0x04) — missing
			 * from rocket_registers.h. BSP RKNPU regcmd writes:
			 *   entry 1:  0x2004 ← 0xe routing=0x04  (S_POINTER PP_EN)
			 *   entry 61: 0x200c ← 0x1 routing=0x04  (OPERATION_ENABLE)
			 * Mesa rocket has no concept of this sub-unit, so it never
			 * generates these writes. The sub-unit (likely CSC = Convolution
			 * Sequence Controller) coordinates conv data flow; without
			 * its OP_ENABLE, CNA's pipeline can't complete, so CNA's
			 * OP_ENABLE entry is silently rejected. Inject from kernel. */
			// CPU_PREWRITES_OFF: writel(0xe, _npu + 0x2004);   /* mystery sub-unit S_POINTER */
			// CPU_PREWRITES_OFF: writel(0x1, _npu + 0x200c);   /* mystery sub-unit OP_ENABLE */
			dev_err(core->dev,
				"MYSTERY_INIT: 0x2004=0x%08x 0x200c=0x%08x",
				readl(_npu + 0x2004), readl(_npu + 0x200c));
			/* 2026-05-29 BSP DPU REGISTERS: mesa misses these.
			 * Values from bsp_regcmd_run0_yolov5s.txt. */
			// CPU_PREWRITES_OFF: writel(0x53,       _npu + 0x4060);
			// CPU_PREWRITES_OFF: writel(0x383,      _npu + 0x4070);
			// CPU_PREWRITES_OFF: writel(0x1,        _npu + 0x4078);
			// CPU_PREWRITES_OFF: writel(0xffffff80, _npu + 0x4080);
			// CPU_PREWRITES_OFF: writel(0x4040,     _npu + 0x4084);
			// CPU_PREWRITES_OFF: writel(0x16,       _npu + 0x4088);
			// CPU_PREWRITES_OFF: writel(0x190000,   _npu + 0x40c0);
			dev_err(core->dev,
				"DPU_BSP: 0x4060=0x%x 0x4070=0x%x 0x4078=0x%x 0x4080=0x%x 0x4084=0x%x 0x4088=0x%x 0x40c0=0x%x",
				readl(_npu + 0x4060), readl(_npu + 0x4070),
				readl(_npu + 0x4078), readl(_npu + 0x4080),
				readl(_npu + 0x4084), readl(_npu + 0x4088),
				readl(_npu + 0x40c0));
			iounmap(_npu);
		}
	}

	/* PATCH_E127 v2 block MOVED to after `task = &job->tasks[...]`
	 * (was here but uninitialized task caused oops 2026-05-19) */

	/* OUT_MEMSET removed 2026-05-26: was confusing our diagnostics for 8 days.
	 * The 0xAB pre-fill was being read back as if it were DPU output. */

	/* 2026-05-29 IOVA0_MAP: PC ping-pong mode (task_pp_en=1) reads at IOVA 0
	 * for the second task descriptor array we don't provide. Faults at IOVA 0
	 * cause IRQ flood. Map IOVA 0 → scratch page so reads succeed (returning
	 * zeros, which PC should interpret as "no more tasks"). */
	{
		static int iova0_mapped = 0;
		if (!iova0_mapped && core->attached_domain && core->attached_domain->domain
		    && core->staging_page) {
			phys_addr_t scratch_pa = page_to_phys(core->staging_page);
			int ret = iommu_map(core->attached_domain->domain, 0,
					   scratch_pa, PAGE_SIZE,
					   IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
			dev_err(core->dev, "IOVA0_MAP: scratch PA=0x%llx ret=%d",
				(u64)scratch_pa, ret);
			iova0_mapped = 1;
		}
	}

	/* GO ! */

	task = &job->tasks[job->next_task_idx];
	job->next_task_idx++;

	/* REGCMD_NOP (2026-05-19): zero a configurable range [nop_start, nop_end]
	 * of regcmd entries before submit, to bisect which entry blocks CNA's
	 * OP_ENABLE acceptance. Uses pfn_to_page + flush_dcache_page — the
	 * kernel-canonical cache flush path. Module params nop_start / nop_end
	 * default to -1 (no NOPing). */
	if (core->attached_domain && core->attached_domain->domain) {  /* always patch regcmd */
		phys_addr_t rc_pa = iommu_iova_to_phys(
				core->attached_domain->domain, task->regcmd);

		if (rc_pa) {
			struct page *page = pfn_to_page(rc_pa >> PAGE_SHIFT);
			unsigned int page_off = rc_pa & (PAGE_SIZE - 1);
			void *vaddr = kmap_local_page(page);
			u32 *p = (u32 *)((char *)vaddr + page_off);
			int i;

			/* replay_bsp (2026-06-02): DECISIVE content-vs-HW test. Overwrite the
			 * whole regcmd with BSP's byte-exact 133-entry list, swapping ONLY the 4
			 * real IOVA fields with rocket's BO addresses (harvested from the mesa
			 * regcmd first). Runs FIRST so nop=0..5 can disable ping-pong on the
			 * replayed list for the OFF variant. */
			if (replay_bsp) {
				u32 rk_in = 0, rk_wt = 0, rk_out = 0, rk_rdma = 0;
				int k;
				int old_count = (int)task->regcmd_count;

				for (k = 0; k < (int)task->regcmd_count; k++) {
					u32 off = p[k*2] & 0xffff;
					u32 val = (p[k*2] >> 16) | ((p[k*2 + 1] & 0xffff) << 16);

					if (off == 0x1070) rk_in = val;
					else if (off == 0x1110) rk_wt = val;
					else if (off == 0x4020) rk_out = val;
					else if (off == 0x5020) rk_rdma = val;
				}
				for (k = 0; k < (int)ARRAY_SIZE(BSP_REGCMD); k++) {
					u32 off = BSP_REGCMD[k].off;
					u32 val = BSP_REGCMD[k].val;
					u32 rt  = BSP_REGCMD[k].rt;

					if (off == 0x1070 && rk_in) val = rk_in;
					else if (off == 0x1110 && rk_wt) val = rk_wt;
					else if (off == 0x4020 && rk_out) val = rk_out;
					else if (off == 0x5020 && rk_rdma) val = rk_rdma;
					p[k*2]     = (off & 0xffff) | ((val & 0xffff) << 16);
					p[k*2 + 1] = (rt << 24) | 0x010000 | ((val >> 16) & 0xffff);
				}
				/* zero any stale mesa entries beyond BSP's list (mesa had ~136;
				 * leftover OP_ENABLEs would execute after BSP's clean structure). */
				for (k = (int)ARRAY_SIZE(BSP_REGCMD); k < old_count; k++) {
					p[k*2] = 0; p[k*2 + 1] = 0;
				}
				task->regcmd_count = ARRAY_SIZE(BSP_REGCMD);
				flush_dcache_page(page);
				dev_err(core->dev,
					"REPLAY_BSP: wrote %u BSP entries; IOVAs in=0x%x wt=0x%x out=0x%x rdma=0x%x",
					task->regcmd_count, rk_in, rk_wt, rk_out, rk_rdma);
			}

			if (nop_start >= 0 && nop_end >= nop_start) {
				int s = nop_start;
				int e = (nop_end < (int)task->regcmd_count) ?
					nop_end : (int)task->regcmd_count - 1;

				if (s < (int)task->regcmd_count) {
					for (i = s; i <= e; i++) {
						p[i*2] = 0;
						p[i*2 + 1] = 0;
					}
					dev_err(core->dev,
						"REGCMD_NOP: zeroed [%d..%d] PA=0x%llx",
						s, e, (u64)rc_pa);
				}
			}

			/* 2026-05-30: cmac_open — mesa never emits CMAC OP_ENABLE (0x2008),
			 * and CMAC OP_EN rejects CPU writes (PC-broadcast-only). Append a
			 * CMAC OP_ENABLE entry to the regcmd so PC broadcasts it. */
			if (cmac_open) {
				int idx = (int)task->regcmd_count;
				p[idx*2]     = (1u << 16) | 0x2008;
				p[idx*2 + 1] = (0x04u << 24) | 0x010000;
				task->regcmd_count = idx + 1;
				flush_dcache_page(page);
				dev_err(core->dev,
					"CMAC_OPEN: appended CMAC OP_ENABLE at slot %d (count=%u)",
					idx, task->regcmd_count);
			}

			if (cmac_bsp) {
				int idx = (int)task->regcmd_count;
				/* CMAC S_POINTER = 0xe (ping-pong PP_EN|EXECUTER_PP_EN|PP_MODE),
				 * routing 0x04 — BSP slot 1. mesa never emits this. */
				p[idx*2]     = (0xeu << 16) | 0x2004;
				p[idx*2 + 1] = (0x04u << 24) | 0x010000;
				idx++;
				/* CMAC OP_ENABLE = 1 at BSP offset 0x200c (NOT 0x2008), routing 0x04. */
				p[idx*2]     = (1u << 16) | 0x200c;
				p[idx*2 + 1] = (0x04u << 24) | 0x010000;
				idx++;
				task->regcmd_count = idx;
				flush_dcache_page(page);
				dev_err(core->dev,
					"CMAC_BSP: appended CMAC S_POINTER=0xe(0x2004)+OP_ENABLE=1(0x200c), count=%u",
					task->regcmd_count);
			}

			if (cna_last) {
				int j, found = -1;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					if ((p[j*2] & 0xffff) == 0x1008 &&
					    ((p[j*2 + 1] >> 24) & 0xff) == 0x02) {
						found = j;
						p[j*2] = 0; p[j*2 + 1] = 0; /* NOP original */
					}
				}
				if (found >= 0) {
					int idx = (int)task->regcmd_count;
					p[idx*2]     = (1u << 16) | 0x1008;
					p[idx*2 + 1] = (0x02u << 24) | 0x010000;
					task->regcmd_count = idx + 1;
					flush_dcache_page(page);
					dev_err(core->dev,
						"CNA_LAST: moved CNA OP_ENABLE slot %d -> %d", found, idx);
				}
			}

			if (compound_open) {
				int idx = (int)task->regcmd_count;
				/* TRM 64'h0081_0000_007f_0008: w0=0x007f0008 w1=0x00810000 */
				p[idx*2]     = 0x007f0008;
				p[idx*2 + 1] = 0x00810000;
				task->regcmd_count = idx + 1;
				flush_dcache_page(page);
				dev_err(core->dev,
					"COMPOUND_OPEN: appended all-block op_en (0x7f@0x0008) at slot %d",
					idx);
			}

			if (conv_con1 >= 0) {
				int j;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					if ((p[j*2] & 0xffff) == 0x100c) {
						/* in-place: keep offset (w0 low16) + routing/flags
						 * (w1 high16), replace only value field [47:16] */
						p[j*2]     = (p[j*2] & 0x0000ffff) | (((u32)conv_con1 & 0xffff) << 16);
						p[j*2 + 1] = (p[j*2 + 1] & 0xffff0000) | (((u32)conv_con1 >> 16) & 0xffff);
						flush_dcache_page(page);
						dev_err(core->dev, "CONV_CON1_PATCH: slot %d -> 0x%x (w0=0x%08x w1=0x%08x)", j, conv_con1, p[j*2], p[j*2+1]);
					}
				}
			}

			if (cbuf_con0 >= 0) {
				int j;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					if ((p[j*2] & 0xffff) == 0x1040) {
						p[j*2]     = (p[j*2] & 0x0000ffff) | (((u32)cbuf_con0 & 0xffff) << 16);
						p[j*2 + 1] = (p[j*2 + 1] & 0xffff0000) | (((u32)cbuf_con0 >> 16) & 0xffff);
						flush_dcache_page(page);
						dev_err(core->dev, "CBUF_CON0_PATCH: slot %d -> 0x%x", j, cbuf_con0);
					}
				}
			}

			if (bsp_cna) {
				static const struct { u32 off; u32 val; } B[] = {
					/* CNA */
					{0x1010, 0x00000070}, {0x1018, 0x00000000},
					{0x1044, 0x00500028}, {0x104c, 0x0000000a},
					{0x1078, 0x00171c07}, {0x107c, 0x00000050},
					{0x1080, 0x00001900},
					/* ACCU */
					{0x3010, 0x00270027}, {0x3014, 0x0000007f},
					/* DPU */
					{0x4024, 0x00003200}, {0x4040, 0x00000158},
					{0x4054, 0xfffffffb}, {0x4084, 0x00005324},
					{0x40c0, 0x00006400},
					/* DPU_RDMA */
					{0x5028, 0x00000001}, {0x5044, 0x00004000},
				};
				int j, b;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					u32 off = p[j*2] & 0xffff;
					for (b = 0; b < (int)(sizeof(B)/sizeof(B[0])); b++) {
						if (off == B[b].off) {
							p[j*2] = (p[j*2] & 0x0000ffff) | ((B[b].val & 0xffff) << 16);
							p[j*2 + 1] = (p[j*2 + 1] & 0xffff0000) | ((B[b].val >> 16) & 0xffff);
						}
					}
				}
				flush_dcache_page(page);
				dev_err(core->dev, "BSP_CNA: overlaid BSP CNA config values");
			}

			/* 2026-06-01 OP_EN_FIX: clean BSP OP_ENABLE scheme, runs under
			 * clean_run. Byte-exact BSP (our conv): enables at 0x_00c, no CNA
			 * enable, CMAC=1/ACCU=0; DPU(0x400c=0x108)/DPU_RDMA(0x500c=0x27)
			 * already native in mesa config. NO contaminating CONV_CON1/CBUF
			 * values. Encoding matches cmac_open: w0=(val16<<16)|off,
			 * w1=(routing<<24)|0x010000|val_hi16. */
			if (op_en_fix) {
				int j, n_nop = 0, cmac_slot;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					u32 off = p[j*2] & 0xffff;
					switch (off) {
					case 0x1008: /* CNA: BSP emits no enable */
					case 0x4008: /* DPU: real enable 0x400c=0x108 already native */
					case 0x5008: /* DPU_RDMA: real enable 0x500c=0x27 already native */
						p[j*2] = 0; p[j*2 + 1] = 0; n_nop++;
						break;
					case 0x3008: /* ACCU: real enable is 0x300c=0 per BSP */
						p[j*2]     = (0x0u << 16) | 0x300c;
						p[j*2 + 1] = (0x08u << 24) | 0x010000;
						break;
					}
				}
				/* CMAC enable 0x200c=1 (mesa omits it entirely). */
				cmac_slot = (int)task->regcmd_count;
				p[cmac_slot*2]     = (0x1u << 16) | 0x200c;
				p[cmac_slot*2 + 1] = (0x04u << 24) | 0x010000;
				task->regcmd_count = cmac_slot + 1;
				flush_dcache_page(page);
				dev_err(core->dev,
					"OP_EN_FIX: NOPed %d 0x_008 enables, ACCU 0x300c=0, CMAC 0x200c=1 @slot %d (count=%u)",
					n_nop, cmac_slot, task->regcmd_count);
			}

			if (no_block_open) {
				int j, n = 0;
				for (j = 0; j < (int)task->regcmd_count; j++) {
					u32 off = p[j*2] & 0xffff;
					if (off == 0x1008 || off == 0x2008 || off == 0x3008 ||
					    off == 0x4008 || off == 0x5008) {
						p[j*2] = 0; p[j*2 + 1] = 0;
						n++;
					}
				}
				flush_dcache_page(page);
				dev_err(core->dev, "NO_BLOCK_OPEN: NOPed %d per-block op_en entries", n);
			}

			/* 2026-05-28: ROUTING_BYTE_DUMP — for each regcmd entry, print
			 * the routing byte (bits [63:56]) alongside its offset. Filter to
			 * CNA range (0x100c-0x103c) and DPU range (0x4000-0x404c).
			 * Goal: verify CNA entries carry routing=0x02 and DPU entries
			 * carry routing=0x10. If routing differs, we've found the bug. */
			{
				static int routing_dump_done_v3 = 0;
				if (!routing_dump_done_v3) {
					int k;
					/* 2026-05-28: INJECT CMAC OP_ENABLE at entry 7 (redundant DCOMP slot).
				 * Mesa never emits CMAC OP_ENABLE via PC broadcast. CPU pre-write
				 * lands the value in MMIO but maybe PC needs to broadcast it for
				 * sub-unit's internal state machine.
				 * w0 = (value << 16) | offset = (1<<16) | 0x200c = 0x0001200c
				 * w1 = (routing << 24) | 0x010000 = 0x04010000 (routing=0x04 CMAC) */
				if (!clean_run && (int)task->regcmd_count > 7) {
					u32 old_w0 = p[7*2];
					u32 old_w1 = p[7*2 + 1];
					p[7*2]     = 0x0001200c;
					p[7*2 + 1] = 0x04010000;
					dev_err(core->dev,
						"INJECT_CMAC_OPEN at slot 7: old(w0=0x%08x w1=0x%08x) → new(0x0001200c, 0x04010000) = CMAC[0x200c]=1",
						old_w0, old_w1);
				}
				for (k = 0; k < (int)task->regcmd_count; k++) {
						u32 w0 = p[k*2];
						u32 w1 = p[k*2 + 1];
						u32 off = w0 & 0xffff;
						u32 val = (w0 >> 16) | ((w1 & 0xffff) << 16);
						u32 routing = (w1 >> 24) & 0xff;
						if (1) {  /* UNFILTERED: show all 135 entries */
							dev_err(core->dev,
								"ROUTING_DUMP[%d]: off=0x%04x val=0x%08x routing=0x%02x",
								k, off, val, routing);
						}
					}
					routing_dump_done_v3 = 1;
				}
			}

			/* 2026-05-28 OP_EN_OFFSET_FIX: mesa rocket emits OP_ENABLE writes
			 * at offset xxxx008 (CNA/CMAC/ACCU/DPU/DPU_RDMA). BSP YOLOv5s
			 * regcmd capture shows the actual OP_ENABLE registers are at
			 * offset xxxx00c. mesa's writes are silently dropped.
			 *
			 * Patch in-flight:
			 * - off=0x1008 → set to off=0 (no-op; CNA has no software OP_ENABLE)
			 * - off=0x2008 → off=0x200c val=1 (CMAC OP_ENABLE)
			 * - off=0x3008 → off=0x300c val=0 (ACCU OP_ENABLE = 0 explicit disable per BSP)
			 * - off=0x4008 → off=0x400c val=0x108 (DPU OP_ENABLE = BSP value)
			 * - off=0x5008 → off=0x500c val=0x13f (DPU_RDMA OP_ENABLE)
			 */
			{
				int k;
				int patched = 0;
				for (k = 0; k < (int)task->regcmd_count; k++) {
					u32 w0 = p[k*2];
					u32 w1 = p[k*2 + 1];
					u32 off = w0 & 0xffff;
					u32 routing = (w1 >> 24) & 0xff;
					u32 new_off = 0, new_val = 0;
					int do_patch = 0;
					switch (off) {
					case 0x1008:
						/* CNA: no OP_ENABLE per BSP. NOP this entry. */
						new_off = 0; new_val = 0; do_patch = 1;
						break;
					case 0x100c:
						/* CONV_CON1 — BSP writes 0xa000 (constant across YOLOv5s
						 * and MobileNet). Mesa writes 0. Likely CNA conv-mode
						 * enable. Patch to BSP value. */
						new_off = 0x100c; new_val = 0xa000; do_patch = 1;
						break;
					case 0x1040:
						/* CBUF_CON0 — BSP writes 0x17 (constant). Mesa writes
						 * 0x44 (the HW default = no-op). Likely CBUF mode flags
						 * required for CNA conv pipeline. */
						new_off = 0x1040; new_val = 0x17; do_patch = 1;
						break;
					case 0x1024:
						/* DATA_SIZE1 — BSP YOLOv5s and MobileNet BOTH write
						 * 0x00020008 (constant across very different conv shapes).
						 * Mesa writes 0xf0010 — likely wrong format. */
						new_off = 0x1024; new_val = 0x00020008; do_patch = 1;
						break;
					case 0x102c:
						/* DATA_SIZE3 — BSP YOLOv5s 0x1b80, MobileNet 0x1b90
						 * (nearly constant). Mesa writes 0x640 — likely wrong. */
						new_off = 0x102c; new_val = 0x1b80; do_patch = 1;
						break;
					case 0x2008:
						new_off = 0x200c; new_val = 0x1; do_patch = 1;
						break;
					case 0x3008:
						new_off = 0x300c; new_val = 0x0; do_patch = 1;
						break;
					case 0x4008:
						new_off = 0x400c; new_val = 0x108; do_patch = 1;
						break;
					case 0x5008:
						new_off = 0x500c; new_val = 0x13f; do_patch = 1;
						break;
					/* BSP_OVERLAY_v2 2026-05-29: overlay BSP YOLOv5s scalar values for all non-IOVA registers. */
					case 0x1010:
						new_off = 0x1010; new_val = 0x00000040; do_patch = 1;
						break;
					case 0x1014:
						new_off = 0x1014; new_val = 0x00000012; do_patch = 1;
						break;
					case 0x1018:
						new_off = 0x1018; new_val = 0x0003de00; do_patch = 1;
						break;
					case 0x1020:
						new_off = 0x1020; new_val = 0x0280002c; do_patch = 1;
						break;
					case 0x1028:
						new_off = 0x1028; new_val = 0x00000140; do_patch = 1;
						break;
					case 0x1030:
						new_off = 0x1030; new_val = 0x00000180; do_patch = 1;
						break;
					case 0x1034:
						new_off = 0x1034; new_val = 0x00000020; do_patch = 1;
						break;
					case 0x1038:
						new_off = 0x1038; new_val = 0x0202000c; do_patch = 1;
						break;
					case 0x1044:
						new_off = 0x1044; new_val = 0x028000a0; do_patch = 1;
						break;
					case 0x104c:
						new_off = 0x104c; new_val = 0x000e38e1; do_patch = 1;
						break;
					case 0x1050:
						new_off = 0x1050; new_val = 0x4000ff80; do_patch = 1;
						break;
					case 0x1054:
						new_off = 0x1054; new_val = 0x4000ff80; do_patch = 1;
						break;
					case 0x1058:
						new_off = 0x1058; new_val = 0x4000ff80; do_patch = 1;
						break;
					case 0x105c:
						new_off = 0x105c; new_val = 0x00010000; do_patch = 1;
						break;
					case 0x1060:
						new_off = 0x1060; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1064:
						new_off = 0x1064; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1074:
						new_off = 0x1074; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1078:
						new_off = 0x1078; new_val = 0x00171c07; do_patch = 1;
						break;
					case 0x107c:
						new_off = 0x107c; new_val = 0x000000f0; do_patch = 1;
						break;
					case 0x1080:
						new_off = 0x1080; new_val = 0x00064000; do_patch = 1;
						break;
					case 0x1084:
						new_off = 0x1084; new_val = 0x0280002c; do_patch = 1;
						break;
					case 0x1088:
						new_off = 0x1088; new_val = 0x00000008; do_patch = 1;
						break;
					case 0x1100:
						new_off = 0x1100; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1104:
						new_off = 0x1104; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1114:
						new_off = 0x1114; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1118:
						new_off = 0x1118; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x111c:
						new_off = 0x111c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1120:
						new_off = 0x1120; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1124:
						new_off = 0x1124; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1128:
						new_off = 0x1128; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x112c:
						new_off = 0x112c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1130:
						new_off = 0x1130; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1134:
						new_off = 0x1134; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1138:
						new_off = 0x1138; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x113c:
						new_off = 0x113c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1140:
						new_off = 0x1140; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1144:
						new_off = 0x1144; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x1148:
						new_off = 0x1148; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x114c:
						new_off = 0x114c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x3010:
						new_off = 0x3010; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x3014:
						new_off = 0x3014; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x302c:
						new_off = 0x302c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4010:
						new_off = 0x4010; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4014:
						new_off = 0x4014; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4024:
						new_off = 0x4024; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4030:
						new_off = 0x4030; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4034:
						new_off = 0x4034; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4038:
						new_off = 0x4038; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x403c:
						new_off = 0x403c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4040:
						new_off = 0x4040; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4044:
						new_off = 0x4044; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x4048:
						new_off = 0x4048; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x404c:
						new_off = 0x404c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5010:
						new_off = 0x5010; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5014:
						new_off = 0x5014; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x501c:
						new_off = 0x501c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5024:
						new_off = 0x5024; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5028:
						new_off = 0x5028; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5030:
						new_off = 0x5030; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5034:
						new_off = 0x5034; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x503c:
						new_off = 0x503c; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5040:
						new_off = 0x5040; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x5044:
						new_off = 0x5044; new_val = 0x00004000; do_patch = 1;
						break;
					case 0x5048:
						new_off = 0x5048; new_val = 0x00000000; do_patch = 1;
						break;
					case 0x504c:
						new_off = 0x504c; new_val = 0x00000000; do_patch = 1;
						break;
					}
					/* 2026-05-29: BSP's penultimate (0x5064=0) and last (0x5068=0x01010101)
					 * entries both routing=0x20 DPU_RDMA. Mesa never writes these.
					 * 0x5068=0x01010101 is identical across BSP YOLOv5s AND MobileNet —
					 * strong candidate for completion/release trigger.
					 * Hijack our patched slots: turn entry 131 (was CNA OP_EN NOP)
					 * into 0x5068 broadcast, entry 132 (was ACCU OP_EN) into 0x5064. */
					if (k == 134) {
						/* 0x5068 = 0x01010101 routing=0x20 DPU_RDMA — VERY LAST entry per BSP */
						new_off = 0x5068; new_val = 0x01010101; routing = 0x20; do_patch = 1;
					}
					if (k == 133) {
						/* 0x5064 = 0 routing=0x20 — BSP penultimate */
						new_off = 0x5064; new_val = 0x0; routing = 0x20; do_patch = 1;
					}
					if (do_patch && !clean_run) {
						u32 old_w0 = p[k*2];
						u32 old_w1 = p[k*2 + 1];
						/* w0 = (value_low_16 << 16) | offset */
						p[k*2] = ((new_val & 0xffff) << 16) | new_off;
						/* w1 = (routing << 24) | 0x010000 | (value_high_16)
						 * The 0x010000 byte at [16:23] is BSP's consistent
						 * opcode/write-enable flag. */
						p[k*2 + 1] = (routing << 24) | 0x010000 | ((new_val >> 16) & 0xffff);
						dev_err(core->dev,
							"OP_EN_PATCH[%d]: old(w0=0x%08x w1=0x%08x) → new off=0x%04x val=0x%x (w0=0x%08x w1=0x%08x)",
							k, old_w0, old_w1, new_off, new_val, p[k*2], p[k*2+1]);
						patched++;
					}
				}
				if (patched > 0) {
					flush_dcache_page(page);
					dev_err(core->dev,
						"OP_EN_PATCH: patched %d entries", patched);
				}
			}

			if (dcomp_ctrl_val >= 0) {
				/* Find every regcmd entry that writes to
				 * offset 0x1100 (CNA_DCOMP_CTRL) and replace
				 * the value with dcomp_ctrl_val. */
				int patched = 0;
				for (i = 0; i < (int)task->regcmd_count; i++) {
					u32 off = p[i*2] & 0xffff;
					if (off == 0x1100) {
						u32 old_w0 = p[i*2];
						/* word0 = (val_low_16 << 16) | offset */
						p[i*2] = (((u32)dcomp_ctrl_val & 0xffff) << 16) | 0x1100;
						dev_err(core->dev,
							"DCOMP_CTRL_PATCH: entry[%d] w0=0x%08x -> 0x%08x (val=0x%x)",
							i, old_w0, p[i*2], dcomp_ctrl_val);
						patched++;
					}
				}
				if (!patched)
					dev_err(core->dev,
						"DCOMP_CTRL_PATCH: no entries with off=0x1100");
			}

			/* BSP_INJECT 2026-05-19 PM v2: expand to 5 slots by also using
			 * mesa entries [4] and [5] (redundant DPU/DPU_RDMA S_POINTER
			 * writes — we CPU-pre-write these). Now inject 5 BSP-only
			 * critical writes that might trigger CNA EXECUTER state. */
			if (!clean_run && (int)task->regcmd_count >= 129) {
				/* 2026-05-20 PM v3: BSP_INJECT[4] DISABLED.
				 * BSP doesn't write CNA_S_POINTER for single-core RK3568
				 * (num_irqs=1 branch in rknpu_job.c). The PC-broadcasted 0xe
				 * sets EXECUTER_PP_EN (bit 2) in S_POINTER, which mirrors to
				 * S_STATUS RESERVED_1[0] (also bit 2) — the same sticky bit
				 * the CPU pre-write was setting. Match BSP: leave CNA's
				 * S_POINTER untouched. */
				/* if ((p[4*2] & 0xffff) == 0x4004) {
				 *	p[4*2] = 0x000e1004;
				 *	p[4*2 + 1] = 0x02010000;
				 * } */
				/* 2026-05-21 BSP_INJECT[5] DISABLED — testing pure mesa output. */
				/* if ((p[5*2] & 0xffff) == 0x5004) {
				 *	p[5*2] = 0x00171040;
				 *	p[5*2 + 1] = 0x02010000;
				 * } */
				/* 2026-05-21 RE-ENABLED for pipeline test: even though argb_in=0xa
				 * is wrong for 16-channel input, empirically it unlocks the pipeline.
				 * For NOW, get pipeline running to see what output CNA produces. */
				if ((p[126*2] & 0xffff) == 0 && (p[126*2 + 1] >> 24) == 0) {
					p[126*2] = 0xa000100c;
					p[126*2 + 1] = 0x02010000;
					dev_err(core->dev, "BSP_INJECT[126]: 0x100c<-0xa000 (CONV_CON1)");
				}
				/* 2026-05-21 TRM-derived fix: mesa rocket bug — CONV_CON2 emits
				 * only FEATURE_GRAINS, omits KERNEL_GROUP field. Add KERNEL_GROUP=3. */
				if ((p[10*2] & 0xffff) == 0x1010 &&
				    (p[10*2] >> 16) == 0x0350) {
					p[10*2] = (0x0350u << 16) | 0x1010;
					p[10*2 + 1] = 0x02010003;
					dev_err(core->dev,
						"BSP_INJECT[10]: CONV_CON2 OR'd KERNEL_GROUP=3");
				}

				/* 2026-05-21 NEW: CNA_DMA_CON0 fetch_pixel_len bug — mesa
				 * omits the field, leaving it 0. */
				if ((p[28*2] & 0xffff) == 0x1078) {
					u32 old = p[28*2];
					p[28*2] = (0x500fu << 16) | 0x1078;
					p[28*2 + 1] = 0x0201000f;
					dev_err(core->dev,
						"BSP_INJECT[28]: CNA_DMA_CON0=0x000f500f, was 0x%08x", old);
				}

				/* 2026-05-21 BUG #7: CNA_FEATURE_DATA_ADDR (slot 26, off=0x1070)
				 * is set to 0x41000 by mesa — that's the weights2 BO IOVA, not
				 * the input BO. Patch to 0x0 (input BO at start of IOVA space). */
				if ((p[26*2] & 0xffff) == 0x1070) {
					u32 old = p[26*2];
					p[26*2] = 0x00001070;  /* val_low=0, off=0x1070 */
					p[26*2 + 1] = 0x02010000;  /* routing=0x02, val_high=0 */
					dev_err(core->dev,
						"BSP_INJECT[26]: FEATURE_DATA_ADDR=0x0 (input IOVA), was w0=0x%08x", old);
				}
				/* 2026-05-21 REVERTED: injecting CONV_CON2/3/4 at slots 36-38
				 * (overwriting mesa's DCOMP zero-writes 0x1140/0x1144/0x1148)
				 * BROKE everything — CNA went to all zeros, no register writes
				 * landed at all. Mesa's 0x1140 zero-writes are NOT no-ops; CNA
				 * needs them. Can't repurpose those slots. */
				/* 2026-05-21 v2 REVERTED: tested coordinated BSP DPU_RDMA inject
				 * of all 14 register values — DPU_RDMA OP_EN rejected
				 * (was=1, became=0). BSP values tensor-shape-specific. */
				/* SLOT 127 + 128 PRESERVED (2026-05-20 PM finding from
				 * mesa source audit):
				 *
				 *   Slot 127 = mesa's `EMIT(REG_PC_REGISTER_AMOUNTS, 0)`
				 *   Slot 128 = mesa's TRM-mandated magic `0x0041000000000000`
				 *              ("before op_en, 64'h0041_xxxx_xxxx_xxxx must
				 *              be set" — explicit comment in mesa source)
				 *
				 * Previous sessions overwrote these slots with CONV_CON2 and
				 * CONV_CON3 — destroying the TRM-required sentinel and the
				 * PC register-reset. THAT is likely why CNA never reached
				 * EXECUTER. CONV_CON2/3 injection dropped; no safe slots. */
				/* 2026-05-21: BSP_INJECT[16] CBUF_CON0=0x17 DISABLED for test.
				 * BSP YOLOv5s value, may be wrong for conv2d. Let mesa's
				 * native 0x84 land. */
				/* if ((p[16*2] & 0xffff) == 0x1040 &&
				 *     (p[16*2] >> 16) == 0x0084) {
				 *	p[16*2] = 0x00171040;
				 *	p[16*2 + 1] = 0x02010000;
				 * } */
			}

			/* DCOMP_RANGE_INJECT 2026-05-20 (PLAN step 1): inject the
			 * 15-entry DCOMP descriptor zero-write range BSP issues but
			 * mesa rocket doesn't. Shift mesa's OP_ENABLE entries 129-132
			 * forward to positions 144-147 and place 15 DCOMP zero-writes
			 * at positions 129-143. Bump regcmd_count by 15 so PC fetches
			 * all 148 entries.
			 *
			 * The regcmd BO is reused across submits but the rocket_task
			 * struct is fresh per submit (regcmd_count resets to 133), so
			 * we have two cases: (1) first submit — BO has mesa's original
			 * layout at 129, do full shift+inject; (2) subsequent submits —
			 * BO already shifted (CORE_OP_ENABLE now at 144), just bump
			 * the count so PC fetches all 148 entries. */
			/* 2026-05-20 PM v3: TEMPORARILY DISABLE BSP_FULL_INJECT.
			 * After fixing CPU CNA_S_POINTER sticky-bit issue, PC stalled
			 * mid-regcmd. Maybe DCOMP+CVT shift+insert is what stalls PC.
			 * Test with only BSP_INJECT[4/5/16/126] active. */
			if (0 && task->regcmd_count == 133) {
				/* PLAN step 1+2 v2 (TRM-aware): inject DCOMP (15) + CVT (9)
				 * BEFORE the TRM magic sentinel, so TRM magic remains the
				 * last entry before OP_ENABLEs (as a "ready barrier").
				 *
				 * Mesa layout:
				 *   ...
				 *   126: 0x0 no-op (num_tasks==1)
				 *   127: REG_PC_REGISTER_AMOUNTS=0
				 *   128: 0x0041_0000_0000_0000 (TRM magic)
				 *   129-132: 4 OP_ENABLE writes
				 *
				 * New layout:
				 *   0-127: unchanged (incl. PC_REGISTER_AMOUNTS=0)
				 *   128-142: DCOMP_DATA zero-writes (15)
				 *   143-151: CVT_DATA zero-writes (9)
				 *   152: TRM magic (shifted from 128)
				 *   153-156: 4 OP_ENABLEs (shifted from 129-132)
				 */
				/* Fresh: TRM magic at slot 128 (w1 bits 16-31 = 0x0041) */
				bool fresh = (p[128*2] == 0 &&
					      p[128*2 + 1] == 0x00410000);
				bool already = (p[152*2] == 0 &&
						p[152*2 + 1] == 0x00410000);

				if (fresh) {
					static const u32 dcomp_offsets[15] = {
						0x1114, 0x1118, 0x111c, 0x1120, 0x1124,
						0x1128, 0x112c, 0x1130, 0x1134, 0x1138,
						0x113c, 0x1140, 0x1144, 0x1148, 0x114c,
					};
					static const u32 cvt_offsets[9] = {
						0x1210, 0x1214, 0x1218, 0x121c, 0x1220,
						0x1224, 0x1228, 0x122c, 0x1230,
					};
					/* Save 5 mesa entries from positions 128-132
					 * (TRM magic + 4 OP_ENABLEs) */
					u32 saved[10];  /* 5 entries × 2 words */
					int i;

					for (i = 0; i < 10; i++)
						saved[i] = p[128*2 + i];

					/* Inject DCOMP at 128-142 */
					for (i = 0; i < 15; i++) {
						p[(128 + i)*2] = dcomp_offsets[i];
						p[(128 + i)*2 + 1] = 0x02010000;
					}
					/* Inject CVT at 143-151 */
					for (i = 0; i < 9; i++) {
						p[(143 + i)*2] = cvt_offsets[i];
						p[(143 + i)*2 + 1] = 0x02010000;
					}
					/* Restore TRM magic + OP_ENABLEs at 152-156 */
					for (i = 0; i < 10; i++)
						p[152*2 + i] = saved[i];

					task->regcmd_count += 24;
					dev_err(core->dev,
						"BSP_FULL_INJECT v2: FRESH — DCOMP@128-142, CVT@143-151, TRM_MAGIC@152, OP_ENABLEs@153-156, count → %u",
						task->regcmd_count);
				} else if (already) {
					task->regcmd_count += 24;
					dev_err(core->dev,
						"BSP_FULL_INJECT v2: ALREADY_SHIFTED — count → %u",
						task->regcmd_count);
				} else {
					dev_err(core->dev,
						"BSP_FULL_INJECT v2: SKIP — slot128=0x%08x_%08x slot152=0x%08x_%08x",
						p[128*2], p[128*2 + 1],
						p[152*2], p[152*2 + 1]);
				}
			}

			flush_dcache_page(page);
			kunmap_local(vaddr);
		}
	}

	/* TEST 2026-05-18: skip CNA/CORE S_POINTER pre-write for single-core RK3568.
	 * Per BSP rknpu_job.c rknpu_job_subcore_commit_pc lines 308-316: the 0xe
	 * write to 0x1004 + 0x3004 is GATED by `if (num_irqs > 1)`, i.e. multi-core
	 * only (RK3588). For single-core RK3568, BSP does NOT pre-write S_POINTER.
	 * Rocket's pre-write may put CNA/CORE in "pingpong waiting" state that
	 * blocks PC from broadcasting. */
	extra_bit = 0x10000000 * core->index;
	(void)extra_bit;
	if (0) {  /* original pre-write disabled for test */
	rocket_cna_writel(core, S_POINTER, CNA_S_POINTER_POINTER_PP_EN(1) |
					   CNA_S_POINTER_EXECUTER_PP_EN(1) |
					   CNA_S_POINTER_POINTER_PP_MODE(1) |
					   extra_bit);
	rocket_core_writel(core, S_POINTER, CORE_S_POINTER_POINTER_PP_EN(1) |
					    CORE_S_POINTER_EXECUTER_PP_EN(1) |
					    CORE_S_POINTER_POINTER_PP_MODE(1) |
					    extra_bit);
	}  /* end if(0) S_POINTER pre-write disabled */

	/* BSP-aligned submit sequence (2026-05-18). Order + values match
	 * drivers/rknpu/rknpu_job.c rknpu_job_subcore_commit_pc — see also
	 * RKNPU_PC_DATA_EXTRA_AMOUNT=4 (in rknpu_ioctl.h) and the
	 * ((0x6 | pp_en) << pc_task_number_bits) | task_number TASK_CON formula. */
	/* 2026-06-01 pc_slave_pre: BSP rknpu_job_subcore_commit writes 0x1 to
	 * PC_DATA_ADDR (== BASE_ADDRESS 0x10) BEFORE commit_pc overwrites it with the
	 * real regcmd IOVA ("switch to slave mode"). Rocket never did this. This may
	 * reset the PC's data-fetch engine so it actually fetches the regcmd this run.
	 * Source-diff-derived mechanism divergence (content already disproven). */
	if (pc_slave_pre)
		rocket_pc_writel(core, BASE_ADDRESS, 0x1);
	rocket_pc_writel(core, BASE_ADDRESS, task->regcmd);
	/* AMOUNTS: add BSP's EXTRA_AMOUNT (4) so PC has trailing entries to drain pipeline */
	rocket_pc_writel(core, REGISTER_AMOUNTS,
			 PC_REGISTER_AMOUNTS_PC_DATA_AMOUNT(
				 (task->regcmd_count + 4 + scale - 1) / scale - 1));
	/* INT_MASK: include BIT(16) so IOMMU-fault propagates from NPU to GIC in IOMMU mode */
	rocket_pc_writel(core, INTERRUPT_MASK,
			 PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1 | BIT(16));
	/* INT_CLEAR: clear only the bits we mask, not 0xFFFFFFFF (BSP semantics) */
	rocket_pc_writel(core, INTERRUPT_CLEAR,
			 PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1);
	/* TASK_CON: BSP literal ((0x6|pp_en) << 12) | task_number.
	 * BSP YOLOv5s uses task_number 6 or 303 across submissions.
	 * 2026-05-22 tests: 72 (atomic tiles), 0x7<<12|72 (with TASK_PP_EN) —
	 * neither expanded output beyond 1 atomic tile. Reverted to 6. */
	{
		/* TASK_CON = ((0x6 | task_pp_en) << 12) | task_number, BSP formula.
		 * task_pp decouples bit12 from desc_mode (2026-06-02). */
		int taskcon_hi = desc_mode ? 0x7 : 0x6;

		if (task_pp >= 0)
			taskcon_hi = 0x6 | (task_pp ? 1 : 0);
		rocket_pc_writel(core, TASK_CON, (taskcon_hi << 12) | 6);
	}

	/* TASK_DMA_BASE_ADDR: write removed.
	 *
	 * 2026-05-18: pointing at regcmd BO (0xd7000, 4KB) caused IOMMU page
	 * fault at 0x118000 = 0xd7000 + 0x41000. PC's TASK_DMA reads at
	 * offset 0x41000 from base; the 4KB regcmd BO was too small.
	 *
	 * 2026-05-20 PM: tried pointing at input BO (0x41000, 200KB) — large
	 * enough to absorb the read (lands at 0x82000, inside input BO). No
	 * fault, but CNA still doesn't enter EXECUTER state. Confirms TASK_DMA
	 * setup is not the CNA-gating bug. Reverting to no-write. */
	/* 2026-05-28: PC task-descriptor BO write.
	 *
	 * BSP rknpu_job.c writes args->task_base_addr to PC_DMA_BASE_ADDR.
	 * task_base_addr is the IOVA of an array of struct rknpu_task (40 bytes
	 * each). PC's TASK_DMA engine reads descriptors from there; bytes 32-39
	 * of each descriptor are regcmd_addr (IOVA of that task's regcmd buffer).
	 *
	 * Mesa rocket has no descriptor concept — it generates only the regcmd
	 * buffer and never writes PC_DMA_BASE_ADDR. PC then fetches a garbage
	 * descriptor from IOVA 0, reads garbage regcmd_addr, and broadcasts the
	 * wrong (or no) regcmd. This explains the 10-day "CNA configs don't
	 * land" mystery: PC isn't actually broadcasting our regcmd.
	 *
	 * Allocate a coherent 4KB BO on first submit, populate one descriptor
	 * mirroring our regcmd setup, write its IOVA to PC_DMA_BASE_ADDR.
	 */
	if (desc_mode && !core->task_desc_cpu) {
		/* 2026-05-28 v3: dma_alloc_coherent ignored GFP_DMA32 and allocated
		 * a physical page above 4GB. NPU's 32-bit AXI master truncates the
		 * high bits, so descriptor reads landed at random memory. Switch to
		 * explicit __get_free_pages(GFP_DMA32) which uses ZONE_DMA32 directly,
		 * then dma_map_single to get an IOVA in the device's domain.
		 */
		/* 2026-05-28 v4: PC's TASK_DMA engine prefetches at
		 * desc_base - 0x80000 and - 0xc0000 (bisection isolated; constant
		 * regardless of descriptor content). Allocate 1MB and place the
		 * descriptor at offset 0xc0000 from the BO start so both prefetch
		 * IOVAs land inside mapped memory.
		 *
		 * order 8 = 2^8 pages = 1 MB.
		 */
		unsigned long page_ptr = __get_free_pages(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO, 10) /* 4MB: PC TASK_DMA prefetches up to ~0x180000 below desc */;
		if (!page_ptr) {
			dev_err(core->dev, "TASK_DESC v4: __get_free_pages(DMA32, order=8) failed");
		} else {
			dma_addr_t bo_iova = dma_map_single(core->dev,
					(void *)page_ptr, 1 << 22, /* 4 MB */
					DMA_BIDIRECTIONAL);
			if (dma_mapping_error(core->dev, bo_iova)) {
				dev_err(core->dev, "TASK_DESC v4: dma_map_single 1MB failed");
				free_pages(page_ptr, 10);
			} else {
				/* Place descriptor at offset 0xc0000 within the BO */
				core->task_desc_cpu = (void *)(page_ptr + 0x200000); /* 2MB into 4MB BO */
				core->task_desc_iova = bo_iova + 0x200000;
				/* Remember BO start for free path */
				core->task_desc_bo_base = (void *)page_ptr;
				core->task_desc_bo_iova = bo_iova;
				dev_err(core->dev,
					"TASK_DESC v4: bo_base=%p bo_iova=0x%llx | desc_cpu=%p desc_iova=0x%llx desc_PA=0x%llx",
					(void *)page_ptr, (u64)bo_iova,
					core->task_desc_cpu,
					(u64)core->task_desc_iova,
					(u64)virt_to_phys(core->task_desc_cpu));
			}
		}
	}
	if (core->task_desc_cpu) {
		/* struct rknpu_task (40 bytes), packed */
		u32 *d = core->task_desc_cpu;
		u64 *d64 = core->task_desc_cpu;
		memset(core->task_desc_cpu, 0, 40);
		/* 2026-05-28 BISECT: zero ALL fields except regcmd_addr.
		 * If 0x80000-below-descriptor fault pattern persists, it's NOT
		 * driven by descriptor content — must be structural to PC. */
		/* 2026-05-29: restore proper descriptor fields per struct rknpu_task.
		 * Yesterday's BISECT zeroed everything to test the 0x80000-prefetch
		 * pattern — confirmed structural, no longer relevant. Restore proper
		 * values matching BSP semantics. */
		d[0] = 0;                                                /* flags */
		d[1] = 0;                                                /* op_idx */
		d[2] = (u32)desc_en_mask;                                /* enable_mask (param) */
		d[3] = 0xFFFFFFFF; /* int_mask — broaden to all bits (test) */
		d[4] = PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1; /* int_clear */
		d[5] = 0;                                                /* int_status */
		d[6] = task->regcmd_count;                               /* regcfg_amount */
		d[7] = 0;                                                /* regcfg_offset */
		d64[4] = task->regcmd;                                   /* regcmd_addr at offset 32 */
		dev_err(core->dev,
			"TASK_DESC: populated regcmd_addr=0x%llx regcfg_amount=%u",
			task->regcmd, task->regcmd_count);
		/* 2026-05-28 v3: dma_map_single returns a non-coherent mapping.
		 * After CPU populates the descriptor, sync to make it visible to DMA. */
		dma_sync_single_for_device(core->dev, core->task_desc_bo_iova,
					   1 << 22, DMA_BIDIRECTIONAL); /* 4MB BO */
		/* 2026-05-28 v2 DIAGNOSTIC: verify descriptor IOVA reachability.
		 * Compare iova_to_phys for descriptor vs regcmd. If they don't both
		 * resolve via the same domain to valid PAs, dma_alloc_coherent is
		 * using a different page table than Option B2 — explaining the
		 * 0xffd*e* faults from the first task_number=6 test. */
		if (core->attached_domain && core->attached_domain->domain) {
			phys_addr_t desc_pa = iommu_iova_to_phys(
				core->attached_domain->domain, core->task_desc_iova);
			phys_addr_t rc_pa = iommu_iova_to_phys(
				core->attached_domain->domain, task->regcmd);
			u32 *d = core->task_desc_cpu;
			dev_err(core->dev,
				"TASK_DESC v2: desc_iova=0x%llx desc_PA=0x%llx | regcmd_iova=0x%llx regcmd_PA=0x%llx | domain=%px",
				(u64)core->task_desc_iova, (u64)desc_pa,
				(u64)task->regcmd, (u64)rc_pa,
				core->attached_domain->domain);
			dev_err(core->dev,
				"TASK_DESC v2 bytes[0..39]: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
				d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9]);
		}
		/* 2026-05-28 v3: PC_DMA_BASE_ADDR write RE-ENABLED with GFP_DMA32 fix.
		 * v2 diag revealed descriptor PA was above 4GB (NPU 32-bit AXI master
		 * truncates). Now with GFP_DMA32 the BO is allocated below 4GB. */
		rocket_pc_writel(core, TASK_DMA_BASE_ADDR,
				 (u32)core->task_desc_iova);
	}

	/* Read PC register state BEFORE starting PC (safe while idle).
	 * Includes masked INTERRUPT_STATUS — the gate between RAW and the IRQ line.
	 * If RAW has bits set but STATUS stays 0, the mask is blocking propagation.
	 * Pre-OP_EN: also dump INT_MASK and CLEAR readbacks to verify our writes stuck. */
	dev_err(core->dev,
		"CLK_RATES: scmi=%lu clk=%lu aclk=%lu hclk=%lu",
		clk_get_rate(core->clks[0].clk), clk_get_rate(core->clks[1].clk),
		clk_get_rate(core->clks[2].clk), clk_get_rate(core->clks[3].clk));
	dev_err(core->dev,
		"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u AMOUNTS=0x%08x TASKST=0x%08x "
		"INT_MASK=0x%08x INT_CLEAR=0x%08x INT_STATUS=0x%08x INT_RAWST=0x%08x",
		task->regcmd, task->regcmd_count, scale,
		rocket_pc_readl(core, REGISTER_AMOUNTS),
		rocket_pc_readl(core, TASK_STATUS),
		rocket_pc_readl(core, INTERRUPT_MASK),
		rocket_pc_readl(core, INTERRUPT_CLEAR),
		rocket_pc_readl(core, INTERRUPT_STATUS),
		rocket_pc_readl(core, INTERRUPT_RAW_STATUS));

	{
		u32 dte = 0, st = 0, fa = 0, rawst = 0;
		u32 bus_idle = 0xdeadbeef, noc_auto = 0xdeadbeef;
		phys_addr_t regcmd_pa = 0;
		struct regmap *pmu = syscon_regmap_lookup_by_phandle(
				core->dev->of_node, "rockchip,pmu");

		pr_emerg("PBLK: iommu_dev=%px attached=%px domain=%px\n",
			core->iommu_dev, core->attached_domain,
			core->attached_domain ? core->attached_domain->domain : NULL);
		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte, &st, &fa, &rawst);
		if (!IS_ERR_OR_NULL(pmu)) {
			regmap_read(pmu, 0x60, &bus_idle);
			regmap_read(pmu, 0x70, &noc_auto);
		}

		/* IOMMU_EXT (read-only): verify kernel-level AUTO_GATING fix stuck.
		 * Expected AUTO_GATING=0x2 after rockchip-iommu.c writes the value in
		 * rk_iommu_enable() and rk_iommu_resume_paging(). 0x3 means stale. */
		{
			void __iomem *_mmio = ioremap(0xfde4b000, 0x100);
			if (_mmio) {
				dev_err(core->dev,
					"IOMMU_EXT: AUTO_GATING=0x%08x INT_MASK=0x%08x INT_STATUS=0x%08x",
					readl(_mmio + 0x24),
					readl(_mmio + 0x1C),
					readl(_mmio + 0x18));
				iounmap(_mmio);
			} else {
				dev_err(core->dev, "IOMMU_EXT: ioremap(0xfde4b000) failed");
			}
		}

		/* iova_to_phys: verify software page table has the regcmd mapping */
		if (core->attached_domain && core->attached_domain->domain)
			regcmd_pa = iommu_iova_to_phys(
				core->attached_domain->domain, task->regcmd);
		pr_emerg("XDBG_PREOP_IOVA_ENTRY\n");
		dev_err(core->dev,
			"PRE_OP_IOVA: regcmd=0x%llx SW_PA=0x%llx",
			(u64)task->regcmd, (u64)regcmd_pa);

		/* Full regcmd scan: enumerate all entries, look for sub-unit OP_ENABLE
		 * writes (offset 0x1008 CNA, 0x3008 CORE, 0x4008 DPU, 0x6008 PPU).
		 * Absence of any OP_ENABLE explains why no sub-unit completes → no IRQ. */
		if (regcmd_pa) {
			u32 bytes = task->regcmd_count * 8;
			u32 *rcmap = memremap((phys_addr_t)regcmd_pa, bytes, MEMREMAP_WB);
			if (rcmap) {
				bool has_cna_op = false, has_core_op = false;
				bool has_dpu_op = false, has_ppu_op = false;
				u32 i, last_off, last_val;
				dev_err(core->dev,
					"REGCMD_DRAM[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x",
					rcmap[0], rcmap[1], rcmap[2], rcmap[3],
					rcmap[4], rcmap[5], rcmap[6], rcmap[7]);
				/* Each regcmd entry = 64 bits (2 u32). offset is low 16 of word0;
				 * value is bits[47:16] = (word0>>16) | ((word1 & 0xffff) << 16) */
				for (i = 0; i < task->regcmd_count; i++) {
					u32 off = rcmap[i*2] & 0xffff;
					u32 val = (rcmap[i*2] >> 16) | ((rcmap[i*2+1] & 0xffff) << 16);
					if (off == 0x1008) has_cna_op  = true;
					if (off == 0x3008) has_core_op = true;
					if (off == 0x4008) has_dpu_op  = true;
					if (off == 0x6008) has_ppu_op  = true;
					/* Page-aligned values look like IOVAs. */
					if ((val & 0xfff) == 0 && val != 0 && val < 0x10000000)
						dev_err(core->dev,
							"REGCMD_IOVA[%u]: off=0x%04x val=0x%08x", i, off, val);
					/* Widened: dump EVERY regcmd write in CNA/CORE/DPU/DPU_RDMA/PPU range.
					 * Some critical registers (DPU_RDMA SURF_NOTCH/PAD_CFG/SRC_DMA_CFG)
					 * may write past 0x506c. */
					if (off >= 0x1004 && off <= 0x7fff)
						dev_err(core->dev,
							"REGCMD_FULL[%u]: off=0x%04x val=0x%08x", i, off, val);
					/* OP_ENABLE-finder: dump all OP_ENABLE entries + routing byte
					 * + any entry near the end (i >= 120). Routing byte = high byte
					 * of word1 (bits 63:56 of 64-bit entry). */
					if (off == 0x1008 || off == 0x3008 || off == 0x4008 ||
					    off == 0x5008 || off == 0x6008 || i >= 120)
						dev_err(core->dev,
							"REGCMD_OP[%u]: off=0x%04x val=0x%08x routing=0x%02x",
							i, off, val,
							(rcmap[i*2 + 1] >> 24) & 0xff);
				}
				/* INPUT_BO_DUMP: find FEATURE_DATA_ADDR (off=0x1070) value in regcmd,
				 * then dump first 128 bytes of that IOVA via memremap. Reveals
				 * whether user input arrives in the BO that CNA will read. */
				{
					unsigned ii;
					u32 feat_iova = 0, weight_iova = 0;
					for (ii = 0; ii < task->regcmd_count; ii++) {
						u32 o = rcmap[ii*2] & 0xffff;
						u32 v = (rcmap[ii*2] >> 16) | ((rcmap[ii*2 + 1] & 0xffff) << 16);
						if (o == 0x1070) feat_iova = v;
						if (o == 0x1110) weight_iova = v;
					}
					/* OUTPUT_BO sentinel: pre-fill DPU's DST_BASE BO with 0xCC,
				 * then post-job verify if any bytes changed (= DPU wrote there). */
				{
					unsigned ii;
					u32 dpu_dst = 0;
					for (ii = 0; ii < task->regcmd_count; ii++) {
						u32 o = rcmap[ii*2] & 0xffff;
						u32 v = (rcmap[ii*2] >> 16) | ((rcmap[ii*2 + 1] & 0xffff) << 16);
						if (o == 0x4020) dpu_dst = v;
					}
					if (dpu_dst) {
						phys_addr_t dst_pa = iommu_iova_to_phys(
							iommu_get_domain_for_dev(core->dev), dpu_dst);
						if (dst_pa) {
							void *omap = memremap((phys_addr_t)dst_pa, 0x64000, MEMREMAP_WB);
							if (omap) {
								u8 *b = omap;
								dev_err(core->dev,
									"OUTPUT_BO_PRE iova=0x%x: %02x %02x %02x %02x %02x %02x %02x %02x",
									dpu_dst, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
								/* memset removed: was racing with DPU */
								memunmap(omap);
							}
						}
					}
				}
								if (feat_iova) {
						phys_addr_t feat_pa = iommu_iova_to_phys(
							iommu_get_domain_for_dev(core->dev), feat_iova);
						if (feat_pa) {
							void *fmap = memremap((phys_addr_t)feat_pa, 128, MEMREMAP_WB);
							if (fmap) {
								u8 *b = fmap;
								dev_err(core->dev,
									"INPUT_BO iova=0x%x pa=0x%llx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
									feat_iova, (u64)feat_pa,
									b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
									b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
								memunmap(fmap);
							}
						}
					}
					if (weight_iova) {
						phys_addr_t wpa = iommu_iova_to_phys(
							iommu_get_domain_for_dev(core->dev), weight_iova);
						if (wpa) {
							void *wmap = memremap((phys_addr_t)wpa, 128, MEMREMAP_WB);
							if (wmap) {
								u8 *b = wmap;
								dev_err(core->dev,
									"WEIGHT_BO iova=0x%x pa=0x%llx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
									weight_iova, (u64)wpa,
									b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
									b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
								memunmap(wmap);
							}
						}
					}
				}
				last_off = rcmap[(task->regcmd_count - 1)*2] & 0xffff;
				last_val = (rcmap[(task->regcmd_count - 1)*2] >> 16)
				         | ((rcmap[(task->regcmd_count - 1)*2 + 1] & 0xffff) << 16);
				dev_err(core->dev,
					"REGCMD_SCAN: count=%u CNA_OP=%d CORE_OP=%d DPU_OP=%d PPU_OP=%d "
					"LAST_OFF=0x%04x LAST_VAL=0x%08x",
					task->regcmd_count, has_cna_op, has_core_op,
					has_dpu_op, has_ppu_op, last_off, last_val);
				memunmap(rcmap);
			} else {
				dev_err(core->dev, "REGCMD_DRAM: memremap failed PA=0x%llx",
					(u64)regcmd_pa);
			}
		}

		/* Read DTE[0] from DRAM via CPU (memremap=cached linear map);
		 * then follow the DTE to read the PTE entry for task->regcmd.
		 * If DTE/PTE are valid but IOMMU HW walk still stalls -> AXI bus issue. */
		if (dte) {
			u32 *dtmap = memremap((phys_addr_t)dte, 32, MEMREMAP_WB);
			if (dtmap) {
				u32 dte0 = dtmap[0];
				dev_err(core->dev,
					"DTE_DRAM: [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x",
					dte0, dtmap[1], dtmap[2], dtmap[3]);
				memunmap(dtmap);

				/* Follow DTE[0] to read PTE entry for task->regcmd */
				if (dte0 & 1) {
					u32 pte_table_pa = dte0 & ~0x3FFu;
					u32 pte_idx = (task->regcmd >> 12) & 0x3FF;
					u32 *ptemap = memremap(
						(phys_addr_t)pte_table_pa, 0x1000, MEMREMAP_WB);
					if (ptemap) {
						u32 pte_val = ptemap[pte_idx];
						dev_err(core->dev,
							"PTE_DRAM: table=0x%08x idx=0x%03x PTE=0x%08x (expected_pa=0x%llx)",
							pte_table_pa, pte_idx, pte_val,
							(u64)regcmd_pa);
						memunmap(ptemap);
					} else {
						dev_err(core->dev,
							"PTE_DRAM: memremap failed table=0x%08x",
							pte_table_pa);
					}
				} else {
					dev_err(core->dev, "DTE[0]=0x%08x not valid (bit0=0)", dte0);
				}
			} else {
				dev_err(core->dev, "DTE_DRAM: memremap failed PA=0x%08x", dte);
			}
		}

		dev_err(core->dev,
			"PRE_OP: PC_RAWST=0x%08x BUS_IDLE_ST=0x%08x NOC_AUTO=0x%08x IOMMU: DTE=0x%08x ST=0x%08x FA=0x%08x RAWST=0x%08x",
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS),
			bus_idle, noc_auto, dte, st, fa, rawst);
		/* PRE_OP CNA + CORE readback — comparable to BSP RKNPU_DUMP_BEFORE_OPEN.
		 * Check if CNA S_STATUS is already at 0x20004 BEFORE OP_EN fires
		 * (set by our pre-writes), or starts at 0 like BSP and only transitions
		 * to 0x20004 after the OP_ENABLE entry is broadcast. */
		if (core->cna_iomem)
			dev_err(core->dev,
				"PRE_OP CNA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CBUF_CON0=%08x DCOMP_CTRL=%08x",
				readl(core->cna_iomem + 0x00),
				readl(core->cna_iomem + 0x04),
				readl(core->cna_iomem + 0x08),
				readl(core->cna_iomem + 0x40),
				readl(core->cna_iomem + 0x100));
		if (core->core_iomem)
			dev_err(core->dev,
				"PRE_OP CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
				readl(core->core_iomem + 0x00),
				readl(core->core_iomem + 0x04),
				readl(core->core_iomem + 0x08));
	}

	/* BSP-style OP_EN pulse: write 1 then 0 (rknpu_job.c lines 397-398) */
	/* 2026-05-26: aggressive TLB flush right before OP_ENABLE — try to keep
	 * TLB warm for PC's upcoming burst reads of regcmd. */
	if (core->iommu_dev)
		rk_iommu_flush_tlb(core->iommu_dev);


	if (cna_sp >= 0) {
		void __iomem *_nps = ioremap(0xfde40000, 0x10000);
		if (_nps) {
			writel((u32)cna_sp, _nps + 0x1004); /* CNA S_POINTER */
			dev_err(core->dev, "CNA_SP: CPU-wrote CNA S_POINTER=0x%x readback=0x%x", cna_sp, readl(_nps + 0x1004));
			iounmap(_nps);
		}
	}

	/* 2026-06-01 PERF probe: clear the NPU top data-amount counters
	 * (rknpu_old_top_amount: clr_all=0x8010, dt_wr=0x8034, dt_rd=0x8038,
	 * wt_rd=0x803c) right before OP_EN, so a post-job read tells us whether CNA
	 * actually read input/weights (dt_rd/wt_rd != 0) and whether DPU wrote output
	 * (dt_wr != 0). Decisive read-vs-never-read probe; bypasses ambiguous IOMMU
	 * idle bits. */
	{
		void __iomem *_perf = ioremap(0xfde40000, 0x10000);
		if (_perf) {
			/* Vendor enable+clear sequence (rknpu_clear_rw_amount):
			 * 0x80000101 (clear pulse + enable) then 0x00000101 (enable). */
			writel(0x80000101, _perf + 0x8010);
			writel(0x00000101, _perf + 0x8010);
			dev_err(core->dev,
				"PERF_CLR(pre-OP): clr=%08x dt_wr=%08x dt_rd=%08x wt_rd=%08x",
				readl(_perf + 0x8010), readl(_perf + 0x8034),
				readl(_perf + 0x8038), readl(_perf + 0x803c));
			iounmap(_perf);
		}
	}
	rocket_pc_writel(core, OPERATION_ENABLE, 1);
	rocket_pc_writel(core, OPERATION_ENABLE, 0);
	/* 2026-05-29 angle 3b: try double-pulse */
	udelay(100);
	rocket_pc_writel(core, OPERATION_ENABLE, 1);
	rocket_pc_writel(core, OPERATION_ENABLE, 0);
	dev_err(core->dev, "DOUBLE_PULSE: PC OP_EN pulsed twice");

	/* AT_OPEN: read CNA/CORE OP_EN IMMEDIATELY after PC starts, before
	 * any delay. If CNA OP_EN=1 here -> CNA started (may then complete
	 * and clear). If 0 -> CNA's OP_ENABLE broadcast never reached it. */
	{
		void __iomem *_npu = ioremap(0xfde40000, 0x10000);
		if (_npu) {
			dev_err(core->dev,
				"AT_OPEN CNA: OP_EN=%08x  CORE: OP_EN=%08x  DPU: OP_EN=%08x  DPU_RDMA: OP_EN=%08x",
				readl(_npu + 0x1008),  /* CNA_OP_ENABLE */
				readl(_npu + 0x3008),  /* CORE_OP_ENABLE */
				readl(_npu + 0x4008),  /* DPU_OP_ENABLE */
				readl(_npu + 0x5008)); /* DPU_RDMA_OP_ENABLE */
			iounmap(_npu);
		}
	}
	rocket_pipe_sweep(core, "AT_OPEN");

	/* Snapshot right after OP_EN (before 500µs delay) to see if IOMMU goes
	 * busy immediately — indicates PC DMA issued its first IOVA read. */
	{
		u32 dte_imm = 0, st_imm = 0, fa_imm = 0, rawst_imm = 0;
		u32 bus_idle_imm = 0xdeadbeef;
		struct regmap *pmu_imm = syscon_regmap_lookup_by_phandle(
				core->dev->of_node, "rockchip,pmu");

		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte_imm, &st_imm, &fa_imm, &rawst_imm);
		if (!IS_ERR_OR_NULL(pmu_imm))
			regmap_read(pmu_imm, 0x60, &bus_idle_imm);
		dev_err(core->dev,
			"IMM_POST_OPENEN: IOMMU_ST=0x%08x FA=0x%08x BUS_IDLE_ST=0x%08x",
			st_imm, fa_imm, bus_idle_imm);
	}

	/* Sample IOMMU at 50µs: if 0x19 (idle) PC DMA finished; if still 0x01 PC DMA stuck */
	udelay(50);
	{
		u32 st50 = 0, fa50 = 0, dte50 = 0, rawst50 = 0;
		u32 amounts50 = rocket_pc_readl(core, REGISTER_AMOUNTS);

		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte50, &st50, &fa50, &rawst50);
		dev_err(core->dev,
			"AT50US: IOMMU_ST=0x%08x FA=0x%08x RAWST=0x%08x PC_AMOUNTS=0x%08x PC_RS=0x%08x",
			st50, fa50, rawst50, amounts50,
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS));
		if (core->cna_iomem)
			dev_err(core->dev,
				"AT50US CNA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CBUF_CON0=%08x DCOMP_CTRL=%08x",
				readl(core->cna_iomem + 0x00), readl(core->cna_iomem + 0x04),
				readl(core->cna_iomem + 0x08), readl(core->cna_iomem + 0x40),
				readl(core->cna_iomem + 0x100));
		if (core->core_iomem)
			dev_err(core->dev,
				"AT50US CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
				readl(core->core_iomem + 0x00),
				readl(core->core_iomem + 0x04),
				readl(core->core_iomem + 0x08));
		rocket_pipe_sweep(core, "AT50US");
	}

	/* Multi-point IOMMU STATUS sampling: 1ms/5ms/10ms after OP_EN */
	msleep(1);
	{
		u32 st1 = 0, fa1 = 0, dte1 = 0, rawst1 = 0;
		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte1, &st1, &fa1, &rawst1);
		dev_err(core->dev,
			"AT1MS: IOMMU_ST=0x%08x FA=0x%08x PC_AMOUNTS=0x%08x PC_RS=0x%08x TASKST=0x%08x",
			st1, fa1,
			rocket_pc_readl(core, REGISTER_AMOUNTS),
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS),
			rocket_pc_readl(core, TASK_STATUS));
		if (core->cna_iomem)
			dev_err(core->dev,
				"AT1MS CNA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CBUF_CON0=%08x",
				readl(core->cna_iomem + 0x00), readl(core->cna_iomem + 0x04),
				readl(core->cna_iomem + 0x08), readl(core->cna_iomem + 0x40));
		if (core->core_iomem)
			dev_err(core->dev,
				"AT1MS CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
				readl(core->core_iomem + 0x00),
				readl(core->core_iomem + 0x04),
				readl(core->core_iomem + 0x08));
		rocket_pipe_sweep(core, "AT1MS");
	}
	msleep(4);
	{
		u32 st5 = 0, fa5 = 0, dte5 = 0, rawst5 = 0;
		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte5, &st5, &fa5, &rawst5);
		dev_err(core->dev,
			"AT5MS: IOMMU_ST=0x%08x FA=0x%08x PC_AMOUNTS=0x%08x PC_RS=0x%08x TASKST=0x%08x",
			st5, fa5,
			rocket_pc_readl(core, REGISTER_AMOUNTS),
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS),
			rocket_pc_readl(core, TASK_STATUS));
		if (core->cna_iomem)
			dev_err(core->dev,
				"AT5MS CNA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CBUF_CON0=%08x",
				readl(core->cna_iomem + 0x00), readl(core->cna_iomem + 0x04),
				readl(core->cna_iomem + 0x08), readl(core->cna_iomem + 0x40));
		if (core->core_iomem)
			dev_err(core->dev,
				"AT5MS CORE: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
				readl(core->core_iomem + 0x00),
				readl(core->core_iomem + 0x04),
				readl(core->core_iomem + 0x08));
		rocket_pipe_sweep(core, "AT5MS");

	}
	msleep(5);
	{
		u32 dte = 0, st = 0, fa = 0, rawst = 0;
		u32 bus_idle = 0xdeadbeef, noc_auto = 0xdeadbeef;
		struct regmap *pmu = syscon_regmap_lookup_by_phandle(
				core->dev->of_node, "rockchip,pmu");
		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte, &st, &fa, &rawst);
		if (!IS_ERR_OR_NULL(pmu)) {
			regmap_read(pmu, 0x60, &bus_idle);
			regmap_read(pmu, 0x70, &noc_auto);
		}
		dev_err(core->dev,
			"AT10MS: PC_RS=0x%08x PC_ISTATUS=0x%08x PC_OPENA=0x%08x PC_BASE=0x%08x PC_AMOUNTS=0x%08x PC_TASKST=0x%08x",
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS),
			rocket_pc_readl(core, INTERRUPT_STATUS),
			rocket_pc_readl(core, OPERATION_ENABLE),
			rocket_pc_readl(core, BASE_ADDRESS),
			rocket_pc_readl(core, REGISTER_AMOUNTS),
			rocket_pc_readl(core, TASK_STATUS));
		dev_err(core->dev,
			"AT10MS: IOMMU: DTE=0x%08x ST=0x%08x FA=0x%08x RAWST=0x%08x BUS_IDLE_ST=0x%08x NOC_AUTO=0x%08x",
			dte, st, fa, rawst, bus_idle, noc_auto);
		rocket_pipe_sweep(core, "AT10MS");
		/* Full PC register dump (offsets 0x00..0x3c). VERSION at 0/4
		 * tells us PC's firmware/hardware revision. Other reads reveal
		 * any HW status bits we missed. */
		{
			void __iomem *_pc = core->pc_iomem;
			if (_pc) {
				dev_err(core->dev,
					"AT10MS PCDUMP: VER=%08x VERNUM=%08x OPENA=%08x BASE=%08x AMOUNTS=%08x "
					"MASK=%08x CLEAR=%08x STATUS=%08x RAW=%08x TASKCON=%08x TASKDMA=%08x TASKST=%08x",
					readl(_pc + 0x00), readl(_pc + 0x04),
					readl(_pc + 0x08), readl(_pc + 0x10),
					readl(_pc + 0x14), readl(_pc + 0x20),
					readl(_pc + 0x24), readl(_pc + 0x28),
					readl(_pc + 0x2c), readl(_pc + 0x30),
					readl(_pc + 0x34), readl(_pc + 0x3c));
			}
			/* CNA/CORE sub-unit register snapshot at AT10MS.
			 * If PC executed the regcmd, CNA_CBUF_CON0 should be 0x84 (regcmd entry 0).
			 * If CNA registers show defaults, PC never broadcast anything. */
			if (core->cna_iomem) {
				dev_err(core->dev,
					"AT10MS CNADUMP: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x CONV_CON1=%08x "
					"CBUF_CON0=%08x DCOMP_CTRL=%08x",
					readl(core->cna_iomem + 0x00),   /* CNA_S_STATUS @ 0x1000 */
					readl(core->cna_iomem + 0x04),   /* CNA_S_POINTER @ 0x1004 */
					readl(core->cna_iomem + 0x08),   /* CNA_OP_ENABLE @ 0x1008 */
					readl(core->cna_iomem + 0x0c),   /* CNA_CONV_CON1 @ 0x100c */
					readl(core->cna_iomem + 0x40),   /* CNA_CBUF_CON0 @ 0x1040 (regcmd entry 0 target) */
					readl(core->cna_iomem + 0x100)); /* CNA_DCOMP_CTRL @ 0x1100 */
			}
			/* CNA debug register scan: gap 0x1094-0x10FC.
			 * TRM mentions cna_dc_status (buffer full/empty + fetch status)
			 * and cna_csc_status (which condition missed for normal work)
			 * but offsets not documented. Scan and log non-zero values. */
			if (core->cna_iomem) {
				int _off;
				char _buf[400];
				int _len;
				_len = scnprintf(_buf, sizeof(_buf), "AT10MS CNA_DBG[90-1fc]:");
				for (_off = 0x90; _off < 0x200; _off += 4) {
					u32 _v = readl(core->cna_iomem + _off);
					if (_v != 0)
						_len += scnprintf(_buf + _len, sizeof(_buf) - _len,
							" %03x=%08x", _off + 0x1000, _v);
				}
				dev_err(core->dev, "%s", _buf);
				_len = scnprintf(_buf, sizeof(_buf), "AT10MS CNA_DBG[200-2fc]:");
				for (_off = 0x200; _off < 0x300; _off += 4) {
					u32 _v = readl(core->cna_iomem + _off);
					if (_v != 0)
						_len += scnprintf(_buf + _len, sizeof(_buf) - _len,
							" %03x=%08x", _off + 0x1000, _v);
				}
				dev_err(core->dev, "%s", _buf);
			}
			if (core->core_iomem) {
				dev_err(core->dev,
					"AT10MS COREDUMP: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
					readl(core->core_iomem + 0x00),  /* CORE_S_STATUS @ 0x3000 */
					readl(core->core_iomem + 0x04),  /* CORE_S_POINTER @ 0x3004 */
					readl(core->core_iomem + 0x08)); /* CORE_OP_ENABLE @ 0x3008 */
			}
			/* DPU/DPU_RDMA via full NPU ioremap (rocket lacks dpu_iomem). */
			{
				void __iomem *_npu = ioremap(0xfde40000, 0x10000);
				if (_npu) {
					dev_err(core->dev,
						"AT10MS OPEN_C: CMAC[0x200c]=0x%x ACCU[0x300c]=0x%x DPU[0x400c]=0x%x DPU_RDMA[0x500c]=0x%x",
						readl(_npu + 0x200c), readl(_npu + 0x300c),
						readl(_npu + 0x400c), readl(_npu + 0x500c));
					dev_err(core->dev,
						"AT10MS OPEN_8 (run status): CMAC[0x2008]=0x%x ACCU[0x3008]=0x%x DPU[0x4008]=0x%x DPU_RDMA[0x5008]=0x%x CNA[0x1008]=0x%x",
						readl(_npu + 0x2008), readl(_npu + 0x3008),
						readl(_npu + 0x4008), readl(_npu + 0x5008),
						readl(_npu + 0x1008));
					/* 2026-06-01 PERF probe (post-execution): top data-amount
					 * counters cleared at PRE_OP. dt_rd/wt_rd != 0 ⇒ CNA actually
					 * read input/weights; dt_wr != 0 ⇒ DPU wrote output. Decisive
					 * "did anything move" signal independent of IOMMU idle bits. */
					dev_err(core->dev,
						"AT10MS PERF_POST: dt_wr(0x8034)=%08x dt_rd(0x8038)=%08x wt_rd(0x803c)=%08x clr(0x8010)=%08x",
						readl(_npu + 0x8034), readl(_npu + 0x8038),
						readl(_npu + 0x803c), readl(_npu + 0x8010));
					dev_err(core->dev,
						"AT10MS DPU: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x DST_BASE=%08x",
						readl(_npu + 0x4000), readl(_npu + 0x4004),
						readl(_npu + 0x4008), readl(_npu + 0x4020));
					dev_err(core->dev,
						"AT10MS DPU_RDMA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
						readl(_npu + 0x5000), readl(_npu + 0x5004),
						readl(_npu + 0x5008));
					/* 2026-05-21: FULL DPU_RDMA config dump (regs at 0x500c..0x5040,
					 * covering all that BSP writes per regcmd entries 114-127). */
					dev_err(core->dev,
						"AT10MS DPU_RDMA cfg: 500c=%08x 5010=%08x 5014=%08x 5018=%08x 501c=%08x 5020=%08x",
						readl(_npu + 0x500c), readl(_npu + 0x5010),
						readl(_npu + 0x5014), readl(_npu + 0x5018),
						readl(_npu + 0x501c), readl(_npu + 0x5020));
					dev_err(core->dev,
						"AT10MS DPU_RDMA cfg: 5024=%08x 5028=%08x 502c=%08x 5030=%08x 5034=%08x 5038=%08x 503c=%08x 5040=%08x",
						readl(_npu + 0x5024), readl(_npu + 0x5028),
						readl(_npu + 0x502c), readl(_npu + 0x5030),
						readl(_npu + 0x5034), readl(_npu + 0x5038),
						readl(_npu + 0x503c), readl(_npu + 0x5040));
					/* AT10MS DCOMP (CNA weight decompressor): 0x110c+ */
					dev_err(core->dev,
						"AT10MS DCOMP_CTRL/REGNUM/ADDR0/AMOUNT0: 0x1100=%08x 0x1104=%08x 0x1110=%08x 0x1140=%08x",
						readl(_npu + 0x1100), readl(_npu + 0x1104),
						readl(_npu + 0x1110), readl(_npu + 0x1140));
					/* AT10MS CNA CONV_CON1-4 verify */
					dev_err(core->dev,
						"AT10MS CNA CONV: 0x100c=%08x 0x1010=%08x 0x1014=%08x 0x1018=%08x",
						readl(_npu + 0x100c), readl(_npu + 0x1010),
						readl(_npu + 0x1014), readl(_npu + 0x1018));
					/* AT10MS DATA_SIZE0/1/2 + WEIGHT_SIZE0/1/2 */
					dev_err(core->dev,
						"AT10MS DATA/WEIGHT: 0x1020=%08x 0x1024=%08x 0x1028=%08x 0x1030=%08x 0x1034=%08x 0x1038=%08x",
						readl(_npu + 0x1020), readl(_npu + 0x1024),
						readl(_npu + 0x1028), readl(_npu + 0x1030),
						readl(_npu + 0x1034), readl(_npu + 0x1038));
					/* WEIGHT BO sniff at IOVA 0xe000 (weights1) — read first 8 bytes. */
					if (core->attached_domain && core->attached_domain->domain) {
						phys_addr_t wpa = iommu_iova_to_phys(
							core->attached_domain->domain, 0xe000);
						if (wpa) {
							void *vaddr = memremap(wpa, 32, MEMREMAP_WB);
							if (vaddr) {
								u8 *b = vaddr;
								dev_err(core->dev,
									"WEIGHT@0xe000 first 16: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
									b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
									b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
								memunmap(vaddr);
							}
						}
					}
					iounmap(_npu);
				}
			}
		}
	}
}

static int rocket_acquire_object_fences(struct drm_gem_object **bos,
					int bo_count,
					struct drm_sched_job *job,
					bool is_write)
{
	int i, ret;

	for (i = 0; i < bo_count; i++) {
		ret = dma_resv_reserve_fences(bos[i]->resv, 1);
		if (ret)
			return ret;

		ret = drm_sched_job_add_implicit_dependencies(job, bos[i],
							      is_write);
		if (ret)
			return ret;
	}

	return 0;
}

static void rocket_attach_object_fences(struct drm_gem_object **bos,
					int bo_count,
					struct dma_fence *fence)
{
	int i;

	for (i = 0; i < bo_count; i++)
		dma_resv_add_fence(bos[i]->resv, fence, DMA_RESV_USAGE_WRITE);
}

static int rocket_job_push(struct rocket_job *job)
{
	struct rocket_device *rdev = job->rdev;
	struct drm_gem_object **bos;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	bos = kvmalloc_array(job->in_bo_count + job->out_bo_count, sizeof(void *),
			     GFP_KERNEL);
	memcpy(bos, job->in_bos, job->in_bo_count * sizeof(void *));
	memcpy(&bos[job->in_bo_count], job->out_bos, job->out_bo_count * sizeof(void *));

	ret = drm_gem_lock_reservations(bos, job->in_bo_count + job->out_bo_count, &acquire_ctx);
	if (ret)
		goto err;

	scoped_guard(mutex, &rdev->sched_lock) {
		drm_sched_job_arm(&job->base);

		job->inference_done_fence = dma_fence_get(&job->base.s_fence->finished);

		ret = rocket_acquire_object_fences(job->in_bos, job->in_bo_count, &job->base, false);
		if (ret)
			goto err_unlock;

		ret = rocket_acquire_object_fences(job->out_bos, job->out_bo_count, &job->base, true);
		if (ret)
			goto err_unlock;

		kref_get(&job->refcount); /* put by scheduler job completion */

		drm_sched_entity_push_job(&job->base);
	}

	rocket_attach_object_fences(job->out_bos, job->out_bo_count, job->inference_done_fence);

err_unlock:
	drm_gem_unlock_reservations(bos, job->in_bo_count + job->out_bo_count, &acquire_ctx);
err:
	kvfree(bos);

	return ret;
}

static void rocket_job_cleanup(struct kref *ref)
{
	struct rocket_job *job = container_of(ref, struct rocket_job,
						refcount);
	unsigned int i;

	rocket_iommu_domain_put(job->domain);

	dma_fence_put(job->done_fence);
	dma_fence_put(job->inference_done_fence);

	if (job->in_bos) {
		for (i = 0; i < job->in_bo_count; i++)
			drm_gem_object_put(job->in_bos[i]);

		kvfree(job->in_bos);
	}

	if (job->out_bos) {
		for (i = 0; i < job->out_bo_count; i++)
			drm_gem_object_put(job->out_bos[i]);

		kvfree(job->out_bos);
	}

	kvfree(job->tasks);

	kfree(job);
}

static void rocket_job_put(struct rocket_job *job)
{
	kref_put(&job->refcount, rocket_job_cleanup);
}

static void rocket_job_free(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);

	drm_sched_job_cleanup(sched_job);

	rocket_job_put(job);
}

static struct rocket_core *sched_to_core(struct rocket_device *rdev,
					 struct drm_gpu_scheduler *sched)
{
	unsigned int core;

	for (core = 0; core < rdev->num_cores; core++) {
		if (&rdev->cores[core].sched == sched)
			return &rdev->cores[core];
	}

	return NULL;
}

static struct dma_fence *rocket_job_run(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);
	struct rocket_device *rdev = job->rdev;
	struct rocket_core *core = sched_to_core(rdev, sched_job->sched);
	struct dma_fence *fence = NULL;
	int ret;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	/*
	 * Nothing to execute: can happen if the job has finished while
	 * we were resetting the NPU.
	 */
	if (job->next_task_idx == job->task_count)
		return NULL;

	fence = rocket_fence_create(core);
	if (IS_ERR(fence))
		return fence;

	if (job->done_fence)
		dma_fence_put(job->done_fence);
	job->done_fence = dma_fence_get(fence);

	ret = pm_runtime_get_sync(core->dev);
	if (ret < 0)
		return fence;

	/* CNA_BISECT_T0 — right after pm_runtime_get_sync, BEFORE any IOMMU op. */
	if (core->cna_iomem)
		dev_err(core->dev, "CNA_BISECT_T0(post-pm_get): S_STATUS=%08x",
			readl(core->cna_iomem + 0x00));

	/* Fork B (full_reset==2): vendor full reset before EVERY job submit. The CRU
	 * reset clears the IOMMU regs, so re-program for the already-attached domain
	 * (on job 0 attached_domain is NULL → the normal attach block below handles it). */
	if (full_reset == 2) {
		rocket_full_reset(core);
		if (core->attached_domain && core->iommu_group) {
			iommu_detach_group(core->attached_domain->domain, core->iommu_group);
			iommu_attach_group(core->attached_domain->domain, core->iommu_group);
			if (core->iommu_dev)
				rk_iommu_trigger_enable(core->iommu_dev);
		}
	}

	/*
	 * iommu fix: skip attach if already attached to this domain;
	 * otherwise detach first to clear any stale group->domain pointer
	 * left from a previous modprobe cycle (would cause -EBUSY).
	 * In 6.19, iommu_detach_group() ignores its domain arg and resets
	 * group->domain to group->default_domain unconditionally.
	 */
	if (core->attached_domain != job->domain) {
		dev_err(core->dev, "job_run: detach+attach: attached=%p job=%p\n",
			core->attached_domain, job->domain);
		iommu_detach_group(job->domain->domain, core->iommu_group);
		/* CNA_BISECT_T1 — after detach */
		if (core->cna_iomem)
			dev_err(core->dev, "CNA_BISECT_T1(post-detach): S_STATUS=%08x",
				readl(core->cna_iomem + 0x00));
		ret = iommu_attach_group(job->domain->domain, core->iommu_group);
		if (ret < 0) {
			dev_err(core->dev, "iommu_attach_group failed: %d\n", ret);
			pm_runtime_put_autosuspend(core->dev);
			return fence;
		}
		/* CNA_BISECT_T2 — after attach */
		if (core->cna_iomem)
			dev_err(core->dev, "CNA_BISECT_T2(post-attach): S_STATUS=%08x",
				readl(core->cna_iomem + 0x00));
		/* Hold a kref so attached_domain stays valid until we detach. */
		if (core->attached_domain)
			rocket_iommu_domain_put(core->attached_domain);
		kref_get(&job->domain->kref);
		core->attached_domain = job->domain;

		/*
		 * Complete deferred IOMMU hardware enable.  On RK3568 the IOMMU
		 * device is PM-suspended when iommu_attach_group() is called
		 * (pm_runtime_get_if_in_use(iommu->dev) == 0), so rk_iommu_attach
		 * sets hw_needs_enable=true and skips rk_iommu_enable().  The NPU
		 * bus is now de-idled (runtime_resume ran noc_init before we got
		 * here), so it is safe to enable the IOMMU hardware now.
		 */
		if (core->iommu_dev) {
			pr_emerg("ROCKET: before trigger_enable\n");
			rk_iommu_trigger_enable(core->iommu_dev);
			/* 2026-05-26: explicit TLB flush after enable to ensure
			 * PC AXI master sees fresh page table mappings.
			 * Without this, IOMMU page faults at 0xd7100 (mid-regcmd). */
			rk_iommu_flush_tlb(core->iommu_dev);
			pr_emerg("ROCKET: after trigger_enable\n");
			/* CNA_BISECT_T3 — after trigger_enable */
			if (core->cna_iomem)
				dev_err(core->dev, "CNA_BISECT_T3(post-trigger_enable): S_STATUS=%08x",
					readl(core->cna_iomem + 0x00));
		}
	}

	/* 2026-05-20 LATE: SoC NoC bw_priority + MYSTERY sub-unit + perf-amount
	 * readback from kernel. Compare with BSP DUMP during working YOLOv5s. */
	{
		static bool dumped;
		if (!dumped) {
			void __iomem *bw = ioremap(0xfe180000, 0x100);
			if (bw) {
				dev_err(core->dev,
					"BW_PRIO: 0x00=%08x 0x04=%08x 0x08=%08x 0x0c=%08x 0x10=%08x 0x14=%08x",
					readl(bw + 0x00),
					readl(bw + 0x04),
					readl(bw + 0x08),
					readl(bw + 0x0c),
					readl(bw + 0x10),
					readl(bw + 0x14));
				iounmap(bw);
			} else {
				dev_err(core->dev, "BW_PRIO: ioremap failed");
			}
			/* MYSTERY sub-unit at NPU MMIO offset 0x2xxx via PC iomem */
			{
				void __iomem *npu = ioremap(0xfde40000, 0x10000);
				if (npu) {
					dev_err(core->dev,
						"MYSTERY: 0x2000=%08x 0x2004=%08x 0x2008=%08x 0x200c=%08x",
						readl(npu + 0x2000),
						readl(npu + 0x2004),
						readl(npu + 0x2008),
						readl(npu + 0x200c));
					dev_err(core->dev,
						"PERF: 0x8010=%08x 0x8034=%08x 0x8038=%08x 0x803c=%08x",
						readl(npu + 0x8010),
						readl(npu + 0x8034),
						readl(npu + 0x8038),
						readl(npu + 0x803c));
					iounmap(npu);
				}
			}
			dumped = true;
		}
	}

	pr_emerg("ROCKET: before hw_submit\n");
	scoped_guard(mutex, &core->job_lock) {
		core->in_flight_job = job;
		rocket_job_hw_submit(core, job);
	}
	pr_emerg("ROCKET: after hw_submit\n");

	return fence;
}

static void rocket_job_handle_irq(struct rocket_core *core)
{
	pm_runtime_mark_last_busy(core->dev);

	rocket_pc_writel(core, OPERATION_ENABLE, 0x0);
	rocket_pc_writel(core, INTERRUPT_CLEAR, 0x1ffff);

	scoped_guard(mutex, &core->job_lock)
		if (core->in_flight_job) {
			if (core->in_flight_job->next_task_idx < core->in_flight_job->task_count) {
				rocket_job_hw_submit(core, core->in_flight_job);
				return;
			}

			/*
			 * Do NOT call iommu_detach_group(NULL, ...) here.
			 * On RK3568 the IOMMU cannot be stalled after a job
			 * completes (bus master briefly idle but stall HW locks
			 * all control regs). Passing NULL triggers WARN_ON in
			 * __iommu_group_set_core_domain and panics the board.
			 * The domain stays attached; the next job's
			 * iommu_attach_group() handles any domain switch after
			 * the runtime-resume hardware reset clears paging.
			 */
			dma_fence_signal(core->in_flight_job->done_fence);
			pm_runtime_put_autosuspend(core->dev);
			core->in_flight_job = NULL;
		}
}

static void
rocket_reset(struct rocket_core *core, struct drm_sched_job *bad)
{
	dev_err(core->dev, "rocket_reset: ENTRY pending=%d bad=%p\n",
		atomic_read(&core->reset.pending), bad);
	if (!atomic_read(&core->reset.pending))
		return;
	dev_err(core->dev, "rocket_reset: PAST_PENDING_CHECK\n");

	drm_sched_stop(&core->sched, bad);

	/*
	 * Remaining interrupts have been handled, but we might still have
	 * stuck jobs. Let's make sure the PM counters stay balanced by
	 * manually calling pm_runtime_put_noidle().
	 */
	scoped_guard(mutex, &core->job_lock) {
		if (core->in_flight_job)
			pm_runtime_put_noidle(core->dev);

		/*
		 * Skip iommu_detach_group(NULL, ...) -- see rocket_job_handle_irq.
		 * rocket_core_reset() below hardware-resets the IOMMU (paging OFF),
		 * so the next iommu_attach_group() will stall-skip and succeed.
		 */

		core->in_flight_job = NULL;
	}

	/* Proceed with reset now. */
	rocket_core_reset(core);

	/*
	 * After hardware reset the IOMMU page table walker is cleared.
	 * Re-enable paging WITHOUT force_reset to test whether force_reset
	 * is causing the STATUS=0x01 AXI stall on subsequent jobs.
	 * Keep core->attached_domain — no detach/attach cycle needed.
	 */
	if (core->attached_domain && core->iommu_dev) {
		int err = rk_iommu_resume_paging(core->iommu_dev);

		dev_err(core->dev, "rocket_reset: resume_paging ret=%d\n", err);
	} else {
		dev_err(core->dev, "rocket_reset: no attached_domain or iommu_dev, skipping resume\n");
	}

	/* NPU has been reset, we can clear the reset pending bit. */
	atomic_set(&core->reset.pending, 0);

	/* Restart the scheduler */
	drm_sched_start(&core->sched, 0);
}

static enum drm_gpu_sched_stat rocket_job_timedout(struct drm_sched_job *sched_job)
{
	struct rocket_job *job = to_rocket_job(sched_job);
	struct rocket_device *rdev = job->rdev;
	struct rocket_core *core = sched_to_core(rdev, sched_job->sched);

	dev_err(core->dev, "NPU job timed out");
	{
		u32 dte = 0, st = 0, fa = 0, rawst = 0;

		if (core->iommu_dev)
			rk_iommu_read_status(core->iommu_dev, &dte, &st, &fa, &rawst);
		dev_err(core->dev,
			"TIMEOUT PC_RS=0x%08x TASKST=0x%08x IOMMU: DTE=0x%08x ST=0x%08x FA=0x%08x RAWST=0x%08x",
			rocket_pc_readl(core, INTERRUPT_RAW_STATUS),
			rocket_pc_readl(core, TASK_STATUS),
			dte, st, fa, rawst);
		/* POST_TIMEOUT_CNA: verify ping-pong hypothesis — read CNA configs
		 * AFTER timeout. If S_POINTER swapped during execution, the previously-
		 * inactive group should now be readable. */
		{
			void __iomem *_npu = ioremap(0xfde40000, 0x10000);
			if (_npu) {
				dev_err(core->dev,
					"POST_TIMEOUT_CNA: S_POINTER=%08x CONV_CON1=%08x CONV_CON2=%08x CONV_CON4=%08x DATA_SIZE0=%08x DATA_SIZE3=%08x WEIGHT_SIZE2=%08x",
					readl(_npu + 0x1004),
					readl(_npu + 0x100c),
					readl(_npu + 0x1010),
					readl(_npu + 0x1018),
					readl(_npu + 0x1020),
					readl(_npu + 0x102c),
					readl(_npu + 0x1038));
				/* Toggle S_POINTER bit 0 to force ping-pong swap, then read again */
				writel(0xf, _npu + 0x1004);  /* 0xe|1 = swap to other group */
				dev_err(core->dev,
					"POST_TIMEOUT_CNA_SWAP: S_POINTER=%08x CONV_CON1=%08x CONV_CON2=%08x CONV_CON4=%08x DATA_SIZE0=%08x DATA_SIZE3=%08x WEIGHT_SIZE2=%08x",
					readl(_npu + 0x1004),
					readl(_npu + 0x100c),
					readl(_npu + 0x1010),
					readl(_npu + 0x1018),
					readl(_npu + 0x1020),
					readl(_npu + 0x102c),
					readl(_npu + 0x1038));
				iounmap(_npu);
			}
		}
	}
	/* OUT_MEMSET 2026-05-18: read back output BO start to see what DPU produced.
	 * Pre-submit was filled with 0xAB. Compare here. */
	if (core->attached_domain && core->attached_domain->domain) {
		phys_addr_t out_pa = iommu_iova_to_phys(
				core->attached_domain->domain, 0x73000);
		if (out_pa) {
			void *vmap = memremap(out_pa, 0x1000, MEMREMAP_WB);
			if (vmap) {
				dev_err(core->dev,
					"OUT_MEMSET_POST: out_PA=0x%llx first32=%*ph",
					(u64)out_pa, 32, vmap);
				/* Also sample farther into the output to see if DPU
				 * wrote bigger ranges. Surface stride = 0x6400 (25600);
				 * check end of first surface. */
				dev_err(core->dev,
					"OUT_MEMSET_POST: bytes_at_0x6000=%*ph",
					16, vmap + 0xc00);
				memunmap(vmap);
			}
			/* Check second surface base (0x73000 + 0xc800 = 0x7F800) */
			out_pa = iommu_iova_to_phys(
				core->attached_domain->domain, 0x7F800);
			if (out_pa) {
				void *vmap = memremap(out_pa, 0x40, MEMREMAP_WB);
				if (vmap) {
					dev_err(core->dev,
						"OUT_MEMSET_POST: surf1@0x7F800 first16=%*ph",
						16, vmap);
					memunmap(vmap);
				}
			}
		}
	}
	/* Skip NPU AXI register reads: stalled bus = hard lockup */
	atomic_set(&core->reset.pending, 1);
	rocket_reset(core, sched_job);
	dev_err(core->dev, "TIMEDOUT: rocket_reset returned\n");

	return DRM_GPU_SCHED_STAT_RESET;
}

static void rocket_reset_work(struct work_struct *work)
{
	struct rocket_core *core;

	core = container_of(work, struct rocket_core, reset.work);
	rocket_reset(core, NULL);
}

static const struct drm_sched_backend_ops rocket_sched_ops = {
	.run_job = rocket_job_run,
	.timedout_job = rocket_job_timedout,
	.free_job = rocket_job_free
};

static irqreturn_t rocket_job_irq_handler_thread(int irq, void *data)
{
	struct rocket_core *core = data;

	rocket_job_handle_irq(core);

	return IRQ_HANDLED;
}

static irqreturn_t rocket_job_irq_handler(int irq, void *data)
{
	struct rocket_core *core = data;
	u32 raw_status = rocket_pc_readl(core, INTERRUPT_RAW_STATUS);

	WARN_ON(raw_status & PC_INTERRUPT_RAW_STATUS_DMA_READ_ERROR);
	WARN_ON(raw_status & PC_INTERRUPT_RAW_STATUS_DMA_WRITE_ERROR);

	if (!(raw_status & PC_INTERRUPT_RAW_STATUS_DPU_0 ||
	      raw_status & PC_INTERRUPT_RAW_STATUS_DPU_1))
		return IRQ_NONE;

	rocket_pc_writel(core, INTERRUPT_MASK, 0x0);

	return IRQ_WAKE_THREAD;
}

int rocket_job_init(struct rocket_core *core)
{
	struct drm_sched_init_args args = {
		.ops = &rocket_sched_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = 1,
		.timeout = msecs_to_jiffies(JOB_TIMEOUT_MS),
		.name = dev_name(core->dev),
		.dev = core->dev,
	};
	int ret;

	INIT_WORK(&core->reset.work, rocket_reset_work);
	spin_lock_init(&core->fence_lock);
	mutex_init(&core->job_lock);

	core->irq = platform_get_irq(to_platform_device(core->dev), 0);
	if (core->irq < 0)
		return core->irq;

	ret = devm_request_threaded_irq(core->dev, core->irq,
					rocket_job_irq_handler,
					rocket_job_irq_handler_thread,
					IRQF_SHARED, dev_name(core->dev),
					core);
	if (ret) {
		dev_err(core->dev, "failed to request job irq");
		return ret;
	}

	core->reset.wq = alloc_ordered_workqueue("rocket-reset-%d", 0, core->index);
	if (!core->reset.wq)
		return -ENOMEM;

	core->fence_context = dma_fence_context_alloc(1);

	args.timeout_wq = core->reset.wq;
	ret = drm_sched_init(&core->sched, &args);
	if (ret) {
		dev_err(core->dev, "Failed to create scheduler: %d.", ret);
		goto err_sched;
	}

	return 0;

err_sched:
	drm_sched_fini(&core->sched);

	destroy_workqueue(core->reset.wq);
	return ret;
}

void rocket_job_fini(struct rocket_core *core)
{
	drm_sched_fini(&core->sched);

	cancel_work_sync(&core->reset.work);
	if (core->reset.wq)
		destroy_workqueue(core->reset.wq);
}

int rocket_job_open(struct rocket_file_priv *rocket_priv)
{
	struct rocket_device *rdev = rocket_priv->rdev;
	struct drm_gpu_scheduler **scheds = kmalloc_array(rdev->num_cores,
							  sizeof(*scheds),
							  GFP_KERNEL);
	unsigned int core;
	int ret;

	for (core = 0; core < rdev->num_cores; core++)
		scheds[core] = &rdev->cores[core].sched;

	ret = drm_sched_entity_init(&rocket_priv->sched_entity,
				    DRM_SCHED_PRIORITY_NORMAL,
				    scheds,
				    rdev->num_cores, NULL);
	if (WARN_ON(ret))
		return ret;

	return 0;
}

void rocket_job_close(struct rocket_file_priv *rocket_priv)
{
	struct drm_sched_entity *entity = &rocket_priv->sched_entity;

	kfree(entity->sched_list);
	drm_sched_entity_destroy(entity);
}

int rocket_job_is_idle(struct rocket_core *core)
{
	/* If there are any jobs in this HW queue, we're not idle */
	if (atomic_read(&core->sched.credit_count))
		return false;

	return true;
}

static int rocket_ioctl_submit_job(struct drm_device *dev, struct drm_file *file,
				   struct drm_rocket_job *job)
{
	struct rocket_device *rdev = to_rocket_device(dev);
	struct rocket_file_priv *file_priv = file->driver_priv;
	struct rocket_job *rjob = NULL;
	int ret = 0;

	if (job->task_count == 0)
		return -EINVAL;

	rjob = kzalloc(sizeof(*rjob), GFP_KERNEL);
	if (!rjob)
		return -ENOMEM;

	kref_init(&rjob->refcount);

	rjob->rdev = rdev;

	ret = drm_sched_job_init(&rjob->base,
				 &file_priv->sched_entity,
				 1, NULL, file->client_id);
	if (ret)
		goto out_put_job;

	ret = rocket_copy_tasks(dev, file, job, rjob);
	if (ret)
		goto out_cleanup_job;

	ret = drm_gem_objects_lookup(file, u64_to_user_ptr(job->in_bo_handles),
				     job->in_bo_handle_count, &rjob->in_bos);
	if (ret)
		goto out_cleanup_job;

	rjob->in_bo_count = job->in_bo_handle_count;

	ret = drm_gem_objects_lookup(file, u64_to_user_ptr(job->out_bo_handles),
				     job->out_bo_handle_count, &rjob->out_bos);
	if (ret)
		goto out_cleanup_job;

	rjob->out_bo_count = job->out_bo_handle_count;

	rjob->domain = rocket_iommu_domain_get(file_priv);

	ret = rocket_job_push(rjob);
	if (ret)
		goto out_cleanup_job;

out_cleanup_job:
	if (ret)
		drm_sched_job_cleanup(&rjob->base);
out_put_job:
	rocket_job_put(rjob);

	return ret;
}

int rocket_ioctl_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_submit *args = data;
	struct drm_rocket_job *jobs;
	int ret = 0;
	unsigned int i = 0;

	if (args->job_count == 0)
		return 0;

	if (args->job_struct_size < sizeof(struct drm_rocket_job)) {
		drm_dbg(dev, "job_struct_size field in drm_rocket_submit struct is too small.\n");
		return -EINVAL;
	}

	if (args->reserved != 0) {
		drm_dbg(dev, "Reserved field in drm_rocket_submit struct should be 0.\n");
		return -EINVAL;
	}

	jobs = kvmalloc_array(args->job_count, sizeof(*jobs), GFP_KERNEL);
	if (!jobs) {
		drm_dbg(dev, "Failed to allocate incoming job array\n");
		return -ENOMEM;
	}

	for (i = 0; i < args->job_count; i++) {
		if (copy_from_user(&jobs[i],
				   u64_to_user_ptr(args->jobs) + i * args->job_struct_size,
				   sizeof(*jobs))) {
			ret = -EFAULT;
			drm_dbg(dev, "Failed to copy incoming job array\n");
			goto exit;
		}
	}


	for (i = 0; i < args->job_count; i++)
		rocket_ioctl_submit_job(dev, file, &jobs[i]);

exit:
	kvfree(jobs);

	return ret;
}
