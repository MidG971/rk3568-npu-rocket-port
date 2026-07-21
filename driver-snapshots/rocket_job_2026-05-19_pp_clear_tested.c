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

/* From drivers/iommu/rockchip-iommu.c */
extern int rk_iommu_trigger_enable(struct device *iommu_dev);
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

static void rocket_job_hw_submit(struct rocket_core *core, struct rocket_job *job)
{
	struct rocket_task *task;
	unsigned int extra_bit;
	unsigned int scale = core->soc_data->pc_data_amount_scale;

	/* Don't queue the job if a reset is in progress */
	if (atomic_read(&core->reset.pending))
		return;

	/*
	 * BSP-style soft_reset before every submit (port of rknpu_soft_reset
	 * from drivers/rknpu/rknpu_reset.c). Steps 2-6 of that function:
	 *   2. assert/deassert resets + noc_init  (via rocket_core_reset)
	 *   3. (SCMI rate refresh — done in runtime_resume already)
	 *   4. iommu_detach + iommu_attach  (full rebuild — not just resume_paging)
	 *   5. rk_iommu_trigger_enable      (bring IOMMU out of stall)
	 *
	 * Testing whether full detach/attach (vs just resume_paging) restores
	 * more IOMMU/NoC state machinery that lets PC fetcher start.
	 */
	dev_err(core->dev, "PRE_SUBMIT_RESET: rocket_core_reset");
	rocket_core_reset(core);  /* srst_a + srst_h + noc_init */
	if (core->attached_domain && core->iommu_dev) {
		struct iommu_domain *iommu_d = core->attached_domain->domain;

		iommu_detach_device(iommu_d, core->dev);
		dev_err(core->dev, "PRE_SUBMIT_RESET: detached");
		iommu_attach_device(iommu_d, core->dev);
		dev_err(core->dev, "PRE_SUBMIT_RESET: attached");
		rk_iommu_trigger_enable(core->iommu_dev);
		dev_err(core->dev, "PRE_SUBMIT_RESET: trigger_enable done");
	}

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
			/* CNA_S_POINTER: try PP_CLEAR pulse (bits 4+5 = POINTER_PP_CLEAR +
			 * EXECUTER_PP_CLEAR) — one-shot register to reset pingpong state
			 * which may put CNA into an OP_ENABLE-accepting state.
			 * Keep bit 0 set for the "accept broadcasts" wake we discovered. */
			writel(0x31, _npu + 0x1004);  /* CNA_S_POINTER: PP_CLEAR + wake */
			writel(0x31, _npu + 0x3004);  /* CORE_S_POINTER: same */
			writel(0x1, _npu + 0x4004);   /* DPU_S_POINTER */
			writel(0x1, _npu + 0x5004);   /* DPU_RDMA_S_POINTER */
			iounmap(_npu);
		}
	}

	/* PATCH_E127 v2 block MOVED to after `task = &job->tasks[...]`
	 * (was here but uninitialized task caused oops 2026-05-19) */

	/* OUT_MEMSET 2026-05-18: write 0xAB pattern to first 4KB of output BO
	 * (IOVA 0x73000). After submit completes/times out, read back to see if
	 * DPU wrote anything. If still 0xAB -> DPU never issued writes. If 0x80
	 * (= quantized zero_point) -> DPU wrote zeros (compute wrong). */
	if (core->attached_domain && core->attached_domain->domain) {
		phys_addr_t out_pa = iommu_iova_to_phys(
				core->attached_domain->domain, 0x73000);
		if (out_pa) {
			void *vmap = memremap(out_pa, 0x1000, MEMREMAP_WB);
			if (vmap) {
				memset(vmap, 0xAB, 0x1000);
				dev_err(core->dev,
					"OUT_MEMSET_PRE: out_PA=0x%llx first16=%*ph",
					(u64)out_pa, 16, vmap);
				memunmap(vmap);
			}
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
	if ((nop_start >= 0 || dcomp_ctrl_val >= 0) &&
	    core->attached_domain && core->attached_domain->domain) {
		phys_addr_t rc_pa = iommu_iova_to_phys(
				core->attached_domain->domain, task->regcmd);

		if (rc_pa) {
			struct page *page = pfn_to_page(rc_pa >> PAGE_SHIFT);
			unsigned int page_off = rc_pa & (PAGE_SIZE - 1);
			void *vaddr = kmap_local_page(page);
			u32 *p = (u32 *)((char *)vaddr + page_off);
			int i;

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
	/* TASK_CON: BSP literal ((0x6|pp_en) << 12) | task_number = 0x6001 for single-task */
	rocket_pc_writel(core, TASK_CON, (0x6 << 12) | 1);

	/* TEST 2026-05-18: TASK_DMA_BASE_ADDR write removed (causes 64KB prefetch
	 * fault at 0x118000). Without it, IOMMU stays clean (ST=0x11, FA=0). */
	/* rocket_pc_writel(core, TASK_DMA_BASE_ADDR, task->regcmd); */

	/* Read PC register state BEFORE starting PC (safe while idle).
	 * Includes masked INTERRUPT_STATUS — the gate between RAW and the IRQ line.
	 * If RAW has bits set but STATUS stays 0, the mask is blocking propagation.
	 * Pre-OP_EN: also dump INT_MASK and CLEAR readbacks to verify our writes stuck. */
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
					if (off >= 0x1020 && off <= 0x7fff)
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
	}

	/* BSP-style OP_EN pulse: write 1 then 0 (rknpu_job.c lines 397-398) */
	rocket_pc_writel(core, OPERATION_ENABLE, 1);
	rocket_pc_writel(core, OPERATION_ENABLE, 0);

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
						"AT10MS DPU: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x DST_BASE=%08x",
						readl(_npu + 0x4000), readl(_npu + 0x4004),
						readl(_npu + 0x4008), readl(_npu + 0x4020));
					dev_err(core->dev,
						"AT10MS DPU_RDMA: S_STATUS=%08x S_POINTER=%08x OP_EN=%08x",
						readl(_npu + 0x5000), readl(_npu + 0x5004),
						readl(_npu + 0x5008));
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
		ret = iommu_attach_group(job->domain->domain, core->iommu_group);
		if (ret < 0) {
			dev_err(core->dev, "iommu_attach_group failed: %d\n", ret);
			pm_runtime_put_autosuspend(core->dev);
			return fence;
		}
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
			pr_emerg("ROCKET: after trigger_enable\n");
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
