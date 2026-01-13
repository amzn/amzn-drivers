/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2025 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#include <linux/types.h>

#include "config.h"

#ifndef HAVE_IB_QPT_DRIVER
#define IB_QPT_DRIVER 0xFF
#endif

#if defined(HAVE_DRIVER_ID) && !defined(HAVE_UPSTREAM_EFA)
#define RDMA_DRIVER_EFA 17
#endif

#ifndef HAVE_IBDEV_PRINT
#define ibdev_err(_ibdev, format, arg...) \
	dev_err(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg(_ibdev, format, arg...) \
	dev_dbg(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn(_ibdev, format, arg...) \
	dev_warn(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info(_ibdev, format, arg...) \
	dev_info(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#ifndef HAVE_IBDEV_PRINT_RATELIMITED
#define ibdev_err_ratelimited(_ibdev, format, arg...) \
	dev_err_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_dbg_ratelimited(_ibdev, format, arg...) \
	dev_dbg_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_warn_ratelimited(_ibdev, format, arg...) \
	dev_warn_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#define ibdev_info_ratelimited(_ibdev, format, arg...) \
	dev_info_ratelimited(&((struct ib_device *)(_ibdev))->dev, format, ##arg)
#endif

#ifndef HAVE_IB_PORT_PHYS_STATE_LINK_UP
#define IB_PORT_PHYS_STATE_LINK_UP 5
#endif

#ifndef HAVE_CORE_MMAP_XA
#include <linux/types.h>
#include <linux/device.h>

struct rdma_user_mmap_entry {
	struct ib_ucontext *ucontext;
	unsigned long start_pgoff;
	size_t npages;
};

/* Return the offset (in bytes) the user should pass to libc's mmap() */
static inline u64
rdma_user_mmap_get_offset(const struct rdma_user_mmap_entry *entry)
{
	return (u64)entry->start_pgoff << PAGE_SHIFT;
}

/*
 * Backported kernels don't keep refcnt on entries, hence they should not
 * be removed.
 */
static inline void
rdma_user_mmap_entry_remove(struct rdma_user_mmap_entry *entry)
{
}

static inline void rdma_user_mmap_entry_put(struct rdma_user_mmap_entry *entry)
{
}
#endif

#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#ifndef HAVE_RDMA_NODE_UNSPECIFIED
enum {
	RDMA_NODE_UNSPECIFIED = 7,
};
#endif

#ifndef HAVE_LOG2_BITS_PER
#include <linux/log2.h>

static inline __attribute_const__
int __bits_per(unsigned long n)
{
	if (n < 2)
		return 1;
	if (is_power_of_2(n))
		return order_base_2(n) + 1;
	return order_base_2(n);
}

#define bits_per(n)				\
(						\
	__builtin_constant_p(n) ? (		\
		((n) == 0 || (n) == 1)		\
			? 1 : ilog2(n) + 1	\
	) :					\
	__bits_per(n)				\
)
#endif /* !HAVE_LOG2_BITS_PER */

#ifndef HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE
#include <linux/count_zeros.h>
#include <rdma/ib_umem.h>

/*
 * IB block DMA iterator
 *
 * Iterates the DMA-mapped SGL in contiguous memory blocks aligned
 * to a HW supported page size.
 */
struct ib_block_iter {
	/* internal states */
	struct scatterlist *__sg;	/* sg holding the current aligned block */
	dma_addr_t __dma_addr;		/* unaligned DMA address of this block */
	unsigned int __sg_nents;	/* number of SG entries */
	unsigned int __sg_advance;	/* number of bytes to advance in sg in next step */
	unsigned int __pg_bit;		/* alignment of current block */
};

static inline void __rdma_block_iter_start(struct ib_block_iter *biter,
					   struct scatterlist *sglist, unsigned int nents,
					   unsigned long pgsz)
{
	memset(biter, 0, sizeof(struct ib_block_iter));
	biter->__sg = sglist;
	biter->__sg_nents = nents;

	/* Driver provides best block size to use */
	biter->__pg_bit = __fls(pgsz);
}

static inline bool __rdma_block_iter_next(struct ib_block_iter *biter)
{
	unsigned int block_offset;
	unsigned int delta;

	if (!biter->__sg_nents || !biter->__sg)
		return false;

	biter->__dma_addr = sg_dma_address(biter->__sg) + biter->__sg_advance;
	block_offset = biter->__dma_addr & (BIT_ULL(biter->__pg_bit) - 1);
	delta = BIT_ULL(biter->__pg_bit) - block_offset;

	while (biter->__sg_nents && biter->__sg &&
	       sg_dma_len(biter->__sg) - biter->__sg_advance <= delta) {
		delta -= sg_dma_len(biter->__sg) - biter->__sg_advance;
		biter->__sg_advance = 0;
		biter->__sg = sg_next(biter->__sg);
		biter->__sg_nents--;
	}
	biter->__sg_advance += delta;

	return true;
}

/**
 * rdma_block_iter_dma_address - get the aligned dma address of the current
 * block held by the block iterator.
 * @biter: block iterator holding the memory block
 */
static inline dma_addr_t
rdma_block_iter_dma_address(struct ib_block_iter *biter)
{
	return biter->__dma_addr & ~(BIT_ULL(biter->__pg_bit) - 1);
}

/**
 * rdma_for_each_block - iterate over contiguous memory blocks of the sg list
 * @sglist: sglist to iterate over
 * @biter: block iterator holding the memory block
 * @nents: maximum number of sg entries to iterate over
 * @pgsz: best HW supported page size to use
 *
 * Callers may use rdma_block_iter_dma_address() to get each
 * blocks aligned DMA address.
 */
#define rdma_for_each_block(sglist, biter, nents, pgsz)		\
	for (__rdma_block_iter_start(biter, sglist, nents,	\
				     pgsz);			\
	     __rdma_block_iter_next(biter);)

static inline unsigned long
ib_umem_find_best_pgsz(struct ib_umem *umem,
		       unsigned long pgsz_bitmap,
		       unsigned long virt)
{
	unsigned long curr_len = 0;
	dma_addr_t curr_base = ~0;
	unsigned long va, pgoff;
	struct scatterlist *sg;
	dma_addr_t mask;
	int i;

#ifdef HAVE_IB_UMEM_NUM_DMA_BLOCKS
	umem->iova = va = virt;
#else
	va = virt;
#endif

	/* rdma_for_each_block() has a bug if the page size is smaller than the
	 * page size used to build the umem. For now prevent smaller page sizes
	 * from being returned.
	 */
	pgsz_bitmap &= GENMASK(BITS_PER_LONG - 1, PAGE_SHIFT);

	/* The best result is the smallest page size that results in the minimum
	 * number of required pages. Compute the largest page size that could
	 * work based on VA address bits that don't change.
	 */
	mask = pgsz_bitmap &
	       GENMASK(BITS_PER_LONG - 1,
		       bits_per((umem->length - 1 + virt) ^ virt));
	/* offset into first SGL */
	pgoff = umem->address & ~PAGE_MASK;

#ifdef HAVE_IB_UMEM_SGT_APPEND
	for_each_sgtable_dma_sg(&umem->sgt_append.sgt, sg, i) {
#else
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, i) {
#endif
		/* If the current entry is physically contiguous with the previous
		 * one, no need to take its start addresses into consideration.
		 */
		if (curr_base + curr_len != sg_dma_address(sg)) {

			curr_base = sg_dma_address(sg);
			curr_len = 0;

			/* Reduce max page size if VA/PA bits differ */
			mask |= (curr_base + pgoff) ^ va;

			/* The alignment of any VA matching a discontinuity point
			* in the physical memory sets the maximum possible page
			* size as this must be a starting point of a new page that
			* needs to be aligned.
			*/
			if (i != 0)
				mask |= va;
		}

		curr_len += sg_dma_len(sg);
		va += sg_dma_len(sg) - pgoff;

		pgoff = 0;
	}

	/* The mask accumulates 1's in each position where the VA and physical
	 * address differ, thus the length of trailing 0 is the largest page
	 * size that can pass the VA through to the physical.
	 */
	if (mask)
		pgsz_bitmap &= GENMASK(count_trailing_zeros(mask), 0);
	return pgsz_bitmap ? rounddown_pow_of_two(pgsz_bitmap) : 0;
}
#endif /* !HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE */

#ifndef HAVE_RDMA_UMEM_FOR_EACH_DMA_BLOCK
#include <rdma/ib_umem.h>

static inline void __rdma_umem_block_iter_start(struct ib_block_iter *biter,
						struct ib_umem *umem,
						unsigned long pgsz)
{
	__rdma_block_iter_start(biter, umem->sg_head.sgl, umem->nmap, pgsz);
}

/**
 * rdma_umem_for_each_dma_block - iterate over contiguous DMA blocks of the umem
 * @umem: umem to iterate over
 * @pgsz: Page size to split the list into
 *
 * pgsz must be <= PAGE_SIZE or computed by ib_umem_find_best_pgsz(). The
 * returned DMA blocks will be aligned to pgsz and span the range:
 * ALIGN_DOWN(umem->address, pgsz) to ALIGN(umem->address + umem->length, pgsz)
 *
 * Performs exactly ib_umem_num_dma_blocks() iterations.
 */
#define rdma_umem_for_each_dma_block(umem, biter, pgsz)                        \
	for (__rdma_umem_block_iter_start(biter, umem, pgsz);                  \
	     __rdma_block_iter_next(biter);)
#endif

#ifdef HAVE_U32_PORT
typedef u32 port_t;
#else
typedef u8 port_t;
#endif

#if defined(HAVE_MR_DMABUF) && !defined(HAVE_IB_UMEM_DMABUF_PINNED)
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <rdma/ib_umem.h>

#ifdef HAVE_MODULE_IMPORT_NS
MODULE_IMPORT_NS(DMA_BUF);
#endif

static inline void
ib_umem_dmabuf_unsupported_move_notify(struct dma_buf_attachment *attach)
{
	struct ib_umem_dmabuf *umem_dmabuf = attach->importer_priv;

	ibdev_warn_ratelimited(umem_dmabuf->umem.ibdev,
			       "Invalidate callback should not be called when memory is pinned\n");
}

static struct dma_buf_attach_ops ib_umem_dmabuf_attach_pinned_ops = {
	.allow_peer2peer = true,
	.move_notify = ib_umem_dmabuf_unsupported_move_notify,
};

static inline
struct ib_umem_dmabuf *ib_umem_dmabuf_get_pinned(struct ib_device *device,
						 unsigned long offset,
						 size_t size, int fd,
						 int access)
{
	struct ib_umem_dmabuf *umem_dmabuf;
	int err;

	umem_dmabuf = ib_umem_dmabuf_get(device, offset, size, fd, access,
					 &ib_umem_dmabuf_attach_pinned_ops);
	if (IS_ERR(umem_dmabuf))
		return umem_dmabuf;

	dma_resv_lock(umem_dmabuf->attach->dmabuf->resv, NULL);
	err = dma_buf_pin(umem_dmabuf->attach);
	if (err)
		goto err_release;

	err = ib_umem_dmabuf_map_pages(umem_dmabuf);
	if (err)
		goto err_unpin;
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);

	return umem_dmabuf;

err_unpin:
	dma_buf_unpin(umem_dmabuf->attach);
err_release:
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);
	ib_umem_release(&umem_dmabuf->umem);
	return ERR_PTR(err);
}
#endif /* !HAVE_IB_UMEM_DMABUF_PINNED */

#ifndef HAVE_IB_MR_TYPE
#define IB_MR_TYPE_USER 3
#define IB_MR_TYPE_DMA 4
#endif

#ifndef HAVE_FALLTHROUGH
/*
 * Add the pseudo keyword 'fallthrough' so case statement blocks
 * must end with any of these keywords:
 *   break;
 *   fallthrough;
 *   continue;
 *   goto <label>;
 *   return [expression];
 *
 *  gcc: https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html#Statement-Attributes
 */
#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif
#endif

#ifndef HAVE_IB_UMEM_START_DMA_ADDR
#include <rdma/ib_umem.h>

static inline dma_addr_t ib_umem_start_dma_addr(struct ib_umem *umem)
{
#ifdef HAVE_IB_UMEM_SGT_APPEND
	return sg_dma_address(umem->sgt_append.sgt.sgl) + ib_umem_offset(umem);
#else
	return U64_MAX;
#endif
}
#endif

#ifndef HAVE_IB_UMEM_IS_CONTIGUOUS
#include <rdma/ib_umem.h>

static inline bool ib_umem_is_contiguous(struct ib_umem *umem)
{
	dma_addr_t dma_addr;
	unsigned long pgsz;

	/*
	 * Select the smallest aligned page that can contain the whole umem if
	 * it was contiguous.
	 */
	dma_addr = ib_umem_start_dma_addr(umem);
	pgsz = roundup_pow_of_two((dma_addr ^ (umem->length - 1 + dma_addr)) + 1);
	return !!ib_umem_find_best_pgsz(umem, pgsz, dma_addr);
}
#endif

#if !defined(HAVE_UDATA_TO_UVERBS_ATTR_BUNDLE) && defined(HAVE_ATTR_BUNDLE_DRIVER_UDATA)
#include <rdma/uverbs_ioctl.h>

static inline struct uverbs_attr_bundle *
rdma_udata_to_uverbs_attr_bundle(struct ib_udata *udata)
{
	return container_of(udata, struct uverbs_attr_bundle, driver_udata);
}
#endif

#ifndef HAVE_CREATE_CQ_UMEM
enum efa_uverbs_attrs_create_cq_cmd_attr_ids {
	UVERBS_ATTR_CREATE_CQ_BUFFER_VA = 8,
	UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH,
	UVERBS_ATTR_CREATE_CQ_BUFFER_FD,
	UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET,
};
#endif

#endif /* _KCOMPAT_H_ */
