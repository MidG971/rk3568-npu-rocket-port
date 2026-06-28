// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_print.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/rocket_accel.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "rocket_core.h"
#include "rocket_device.h"
#include "rocket_drv.h"
#include "rocket_job.h"
#include "rocket_registers.h"

/* From drivers/iommu/rockchip-iommu.c */
extern int rk_iommu_trigger_enable(struct device *iommu_dev);

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

	/* GO ! */

	task = &job->tasks[job->next_task_idx];
	job->next_task_idx++;

	rocket_pc_writel(core, BASE_ADDRESS, 0x1);

	 /* From rknpu, in the TRM this bit is marked as reserved */
	extra_bit = 0x10000000 * core->index;
	rocket_cna_writel(core, S_POINTER, CNA_S_POINTER_POINTER_PP_EN(1) |
					   CNA_S_POINTER_EXECUTER_PP_EN(1) |
					   CNA_S_POINTER_POINTER_PP_MODE(1) |
					   extra_bit);

	rocket_core_writel(core, S_POINTER, CORE_S_POINTER_POINTER_PP_EN(1) |
					    CORE_S_POINTER_EXECUTER_PP_EN(1) |
					    CORE_S_POINTER_POINTER_PP_MODE(1) |
					    extra_bit);

	rocket_pc_writel(core, BASE_ADDRESS, task->regcmd);
	rocket_pc_writel(core, REGISTER_AMOUNTS,
			 PC_REGISTER_AMOUNTS_PC_DATA_AMOUNT(
				 (task->regcmd_count + scale - 1) / scale - 1));

	rocket_pc_writel(core, INTERRUPT_MASK, PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1);
	rocket_pc_writel(core, INTERRUPT_CLEAR, PC_INTERRUPT_CLEAR_DPU_0 | PC_INTERRUPT_CLEAR_DPU_1);

	/* BSP uses (0x6 << 12) | 1 = 0x6001: bits 13+14+0, NOT bit 12 (TASK_PP_EN).
	 * Setting TASK_PP_EN enables ping-pong mode which requires a second buffer
	 * that never arrives, silently preventing PC from executing. */
	rocket_pc_writel(core, TASK_CON, PC_TASK_CON_RESERVED_0(1) |
					 PC_TASK_CON_TASK_COUNT_CLEAR(1) |
					 PC_TASK_CON_TASK_NUMBER(1));

	rocket_pc_writel(core, TASK_DMA_BASE_ADDR, PC_TASK_DMA_BASE_ADDR_DMA_BASE_ADDR(0x0));

	/* Read back all PC registers BEFORE OP_EN to verify writes landed in hardware */
	dev_err(core->dev,
		"HW_SUBMIT: regcmd=0x%llx count=%u scale=%u",
		task->regcmd, task->regcmd_count, scale);
	dev_err(core->dev,
		"PC_RB: BASE=0x%08x AMOUNTS=0x%08x MASK=0x%08x TASKCON=0x%08x DMABAS=0x%08x TASKST=0x%08x",
		rocket_pc_readl(core, BASE_ADDRESS),
		rocket_pc_readl(core, REGISTER_AMOUNTS),
		rocket_pc_readl(core, INTERRUPT_MASK),
		rocket_pc_readl(core, TASK_CON),
		rocket_pc_readl(core, TASK_DMA_BASE_ADDR),
		rocket_pc_readl(core, TASK_STATUS));

	/* Dump last 8 regcmd 64-bit entries (16 32-bit words) from DRAM before kicking PC */
	if (core->attached_domain && task->regcmd_count >= 1) {
		phys_addr_t phys = iommu_iova_to_phys(core->attached_domain->domain,
						       (dma_addr_t)task->regcmd);
		if (phys) {
			int total_words = (int)task->regcmd_count * 2;
			int start = max(0, total_words - 16);
			u32 *va = (u32 *)phys_to_virt(phys) + start;

			dev_err(core->dev,
				"REGCMD_TAIL32[%d..%d]: %08x %08x %08x %08x %08x %08x %08x %08x",
				start, total_words - 1,
				va[0], va[1], va[2], va[3],
				va[4], va[5], va[6], va[7]);
			dev_err(core->dev,
				"REGCMD_TAIL32 cont: %08x %08x %08x %08x %08x %08x %08x %08x",
				va[8], va[9], va[10], va[11],
				va[12], va[13], va[14], va[15]);
		}
	}

	rocket_pc_writel(core, OPERATION_ENABLE, PC_OPERATION_ENABLE_OP_EN(1));
	/* Read OP_EN back immediately (before PC starts — within nanoseconds of write) */
	dev_err(core->dev, "OPEN_EN_RB: 0x%08x", rocket_pc_readl(core, OPERATION_ENABLE));

	/* Wait 500us then check PC only — CNA/CORE may be running (AXI reads to them stall CPU) */
	udelay(500);
	dev_err(core->dev, "POST500US: PC_RS=0x%08x",
		rocket_pc_readl(core, INTERRUPT_RAW_STATUS));
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
	 * After hardware reset the IOMMU page table walker is cleared
	 * (paging disabled, DTE_ADDR=0). Update the software IOMMU state
	 * to match so the next iommu_attach_group() does not return -EBUSY.
	 */
	if (core->attached_domain) {
		dev_err(core->dev, "rocket_reset: calling iommu_detach_group, attached=%p\n",
			core->attached_domain);
		iommu_detach_group(core->attached_domain->domain, core->iommu_group);
		dev_err(core->dev, "rocket_reset: iommu_detach_group done\n");
		rocket_iommu_domain_put(core->attached_domain);
		core->attached_domain = NULL;
	} else {
		dev_err(core->dev, "rocket_reset: attached_domain is NULL, skipping detach\n");
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
	/* Skip ALL NPU register reads: if PC/CNA is in DMA fault state the NPU AXI
	 * bus is stalled -- any readl to NPU MMIO hard-lockups the CPU. */
	dev_err(core->dev, "TIMEDOUT: skipping register reads -- CRU reset follows");
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
