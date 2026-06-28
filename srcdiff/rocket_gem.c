// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_device.h>
#include <drm/drm_print.h>
#include <drm/drm_utils.h>
#include <drm/rocket_accel.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/pagemap.h>

#include "rocket_drv.h"
#include "rocket_gem.h"

static void rocket_gem_bo_free(struct drm_gem_object *obj)
{
	struct rocket_gem_object *bo = to_rocket_bo(obj);
	struct rocket_file_priv *rocket_priv = bo->driver_priv;
	size_t unmapped;

	drm_WARN_ON(obj->dev, refcount_read(&bo->base.pages_use_count) > 1);

	unmapped = iommu_unmap(bo->domain->domain, bo->mm.start, bo->size);
	drm_WARN_ON(obj->dev, unmapped != bo->size);

	/* Option B2: free IOVA via cookie's iovad */
	if (bo->iova_pfn) {
		struct iommu_domain *dom = bo->domain->domain;
		struct iova_domain *iovad = (struct iova_domain *)dom->iova_cookie;
		mutex_lock(&rocket_priv->mm_lock);
		if (iovad)
			free_iova_fast(iovad, bo->iova_pfn, bo->iova_size_pfn);
		mutex_unlock(&rocket_priv->mm_lock);
		bo->iova_pfn = 0;
	}

	rocket_iommu_domain_put(bo->domain);
	bo->domain = NULL;

	drm_gem_shmem_free(&bo->base);
}

static const struct drm_gem_object_funcs rocket_gem_funcs = {
	.free = rocket_gem_bo_free,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

struct drm_gem_object *rocket_gem_create_object(struct drm_device *dev, size_t size)
{
	struct rocket_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->base.base.funcs = &rocket_gem_funcs;

	return &obj->base.base;
}

int rocket_ioctl_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;
	struct drm_rocket_create_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;
	struct sg_table *sgt;
	int ret;

	shmem_obj = drm_gem_shmem_create(dev, args->size);
	if (IS_ERR(shmem_obj))
		return PTR_ERR(shmem_obj);

	gem_obj = &shmem_obj->base;
	rkt_obj = to_rocket_bo(gem_obj);

	rkt_obj->driver_priv = rocket_priv;
	rkt_obj->domain = rocket_iommu_domain_get(rocket_priv);
	rkt_obj->size = args->size;
	rkt_obj->offset = 0;

	/*
	 * On RK3568 the NPU AXI bus is 32-bit: physical pages backing GEM
	 * buffers must reside within the first 4 GB of RAM so the IOMMU
	 * translates IOVAs to physical addresses the NPU can actually reach.
	 * Strip the __GFP_HIGHMEM zone bit and add GFP_DMA32 before pages
	 * are allocated by drm_gem_shmem_get_pages_sgt() below.
	 */
	if (to_rocket_device(dev)->cores[0].soc_data->dma_bits <= 32) {
		struct address_space *mapping = shmem_obj->base.filp->f_mapping;

		mapping_set_gfp_mask(mapping,
			(mapping_gfp_mask(mapping) &
			 ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM)) | GFP_DMA32);
	}

	ret = drm_gem_handle_create(file, gem_obj, &args->handle);
	drm_gem_object_put(gem_obj);
	if (ret)
		goto err;

	sgt = drm_gem_shmem_get_pages_sgt(shmem_obj);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err;
	}

	/* TEMP: verify GFP_DMA32 constraint — remove before upstreaming */
	if (sgt->sgl) {
		struct scatterlist *_s;
		int _i, _nents = 0;
		for_each_sgtable_sg(sgt, _s, _i) _nents++;
		drm_err(dev, "BO first page PA=0x%llx size=%u nents=%d sgt->nents=%u sgt->orig_nents=%u\n",
			 (unsigned long long)sg_phys(sgt->sgl), args->size,
			 _nents, sgt->nents, sgt->orig_nents);
	}

	/* 2026-05-26 Option B2: allocate IOVA from device's iova_cookie iovad
	 * (BSP-style), then iommu_map_sg into that contiguous IOVA range. This
	 * uses the IOMMU framework's DMA-managed page table, avoiding the
	 * burst-fault we hit with drm_mm_insert_node + iommu_map_sgtable. */
	{
		struct iommu_domain *dom = rocket_priv->domain->domain;
		struct iova_domain *iovad = (struct iova_domain *)dom->iova_cookie;
		unsigned long iova_pfn;
		dma_addr_t iova;
		unsigned long iova_size = ALIGN(rkt_obj->size, PAGE_SIZE);
		unsigned long limit_pfn = dom->geometry.aperture_end >> PAGE_SHIFT;

		if (!iovad) {
			drm_err(dev, "no iova_cookie on domain — Option B2 needs it\n");
			ret = -EINVAL;
			goto err;
		}

		mutex_lock(&rocket_priv->mm_lock);
		iova_pfn = alloc_iova_fast(iovad, iova_size >> PAGE_SHIFT,
					   limit_pfn, true);
		mutex_unlock(&rocket_priv->mm_lock);

		if (!iova_pfn) {
			drm_err(dev, "alloc_iova_fast failed\n");
			ret = -ENOMEM;
			goto err;
		}

		iova = (dma_addr_t)iova_pfn << PAGE_SHIFT;
		rkt_obj->mm.start = iova;
		rkt_obj->mm.size = iova_size;
		rkt_obj->iova_pfn = iova_pfn;
		rkt_obj->iova_size_pfn = iova_size >> PAGE_SHIFT;
	}

	ret = iommu_map_sgtable(rocket_priv->domain->domain,
				rkt_obj->mm.start,
				shmem_obj->sgt,
				IOMMU_READ | IOMMU_WRITE);
	if (ret < 0 || ret < args->size) {
		drm_err(dev, "failed to map buffer: size=%d request_size=%u\n",
			ret, args->size);
		ret = -ENOMEM;
		goto err_remove_node;
	}

	/* iommu_map_sgtable might have aligned the size */
	rkt_obj->size = ret;
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	args->dma_address = rkt_obj->mm.start;

	/* TEMP DIAG May 17: log IOVA→size→end for every mapped BO.
	 * Use this to determine if IOVA 0x118000 (BUG D page-fault address) is
	 * inside or just beyond a mapped range. */
	drm_info(dev, "BO_MAP: iova=0x%08llx size=0x%x end=0x%08llx pa=0x%llx",
		 (unsigned long long)rkt_obj->mm.start,
		 (unsigned int)rkt_obj->size,
		 (unsigned long long)(rkt_obj->mm.start + rkt_obj->size),
		 (unsigned long long)sg_phys(sgt->sgl));

	return 0;

err_remove_node:
	mutex_lock(&rocket_priv->mm_lock);
	if (rkt_obj->iova_pfn) {
		struct iommu_domain *dom = rocket_priv->domain->domain;
		struct iova_domain *iovad = (struct iova_domain *)dom->iova_cookie;
		if (iovad)
			free_iova_fast(iovad, rkt_obj->iova_pfn, rkt_obj->iova_size_pfn);
		rkt_obj->iova_pfn = 0;
	}
	mutex_unlock(&rocket_priv->mm_lock);

err:
	drm_gem_shmem_object_free(gem_obj);

	return ret;
}

int rocket_ioctl_prep_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_prep_bo *args = data;
	unsigned long timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);
	struct drm_gem_object *gem_obj;
	struct drm_gem_shmem_object *shmem_obj;
	long ret = 0;

	if (args->reserved != 0) {
		drm_dbg(dev, "Reserved field in drm_rocket_prep_bo struct should be 0.\n");
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	ret = dma_resv_wait_timeout(gem_obj->resv, DMA_RESV_USAGE_WRITE, true, timeout);
	if (!ret)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	shmem_obj = &to_rocket_bo(gem_obj)->base;

	dma_sync_sgtable_for_cpu(dev->dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);

	drm_gem_object_put(gem_obj);

	return ret;
}

int rocket_ioctl_fini_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_fini_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;

	if (args->reserved != 0) {
		drm_dbg(dev, "Reserved field in drm_rocket_fini_bo struct should be 0.\n");
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	rkt_obj = to_rocket_bo(gem_obj);
	shmem_obj = &rkt_obj->base;

	dma_sync_sgtable_for_device(dev->dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);

	drm_gem_object_put(gem_obj);

	return 0;
}
