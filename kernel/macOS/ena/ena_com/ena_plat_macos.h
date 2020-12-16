// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ENA_COM_ENA_PLAT_MACOS_H_
#define ENA_COM_ENA_PLAT_MACOS_H_

#include <sys/errno.h>
#include <stdatomic.h>

#include <IOKit/IOLib.h>

#if defined (__cplusplus)
#define ENA_COM_API extern "C"
#else
#define ENA_COM_API
#endif

extern int ena_log_level;

#ifndef container_of
#define container_of(ptr,type,member) ((type*)(((uintptr_t)ptr) - offsetof(type, member)))
#endif

/* Levels */
#define ENA_ALERT 	(1 << 0) /* Alerts are providing more error info.     */
#define ENA_WARNING 	(1 << 1) /* Driver output is more error sensitive.    */
#define ENA_INFO 	(1 << 2) /* Provides additional driver info. 	      */
#define ENA_DBG 	(1 << 3) /* Driver output for debugging.	      */
/* Detailed info that will be printed with ENA_INFO or ENA_DEBUG flag. 	      */
#define ENA_TXPTH 	(1 << 4) /* Allows TX path tracing. 		      */
#define ENA_RXPTH 	(1 << 5) /* Allows RX path tracing.		      */
#define ENA_RSC 	(1 << 6) /* Goes with TXPTH or RXPTH, free/alloc res. */
#define ENA_IOQ 	(1 << 7) /* Detailed info about IO queues. 	      */
#define ENA_ADMQ	(1 << 8) /* Detailed info about admin queue. 	      */

#define ena_trace_raw(ctx, level, fmt, args...)			\
	do {							\
		(void)(ctx);					\
		if (((level) & ena_log_level) != (level))	\
			break;					\
		printf(fmt, ##args);				\
	} while (0)

#define ena_trace(ctx, level, fmt, args...)			\
	ena_trace_raw(ctx, level, "%s(): " fmt " \n", __func__, ##args)

#define ena_trc_dbg(ctx, format, arg...)	ena_trace(ctx, ENA_DBG, format, ##arg)
#define ena_trc_info(ctx, format, arg...)	ena_trace(ctx, ENA_INFO, format, ##arg)
#define ena_trc_warn(ctx, format, arg...)	ena_trace(ctx, ENA_WARNING, format, ##arg)
#define ena_trc_err(ctx, format, arg...)	ena_trace(ctx, ENA_ALERT, format, ##arg)

#define ENA_WARN(cond, ctx, format, arg...)				\
	do {								\
		if (unlikely(cond)) {					\
			ena_trc_err(ctx,				\
				"Warn failed on %s:%s:%d:" format,	\
				__FILE__, __func__, __LINE__, ##arg);	\
		}							\
	} while (0)

#define ENA_PRIu64	"llu"

#define ENA_INTR_INITIAL_TX_INTERVAL_USECS_PLAT 64

#define ENA_MSLEEP(x)	IOSleep(x)
#define ENA_USLEEP(x)	IODelay(x)

#define ENA_MAX32(x,y)	max((x), (y))
#define ENA_MAX16(x,y)	max((x), (y))
#define ENA_MAX8(x, y)	max((x), (y))

#define ENA_MIN32(x,y)	min((x), (y))
#define ENA_MIN16(x,y)	min((x), (y))
#define ENA_MIN8(x, y)	min((x), (y))

#define ENA_UDELAY(x)	IODelay(x)

#define ENA_GET_SYSTEM_TIMEOUT(timeout_us) 				\
	({								\
		AbsoluteTime abstime;					\
		nanoseconds_to_absolutetime((timeout_us)*1000, &abstime); \
		mach_absolute_time() + abstime;				\
	})
#define ENA_TIME_EXPIRE(timeout) ((timeout) < mach_absolute_time())

#define ENA_MIGHT_SLEEP()

#define ENA_REG_WRITE32(bus, value, reg)				\
	({ 								\
		(void)(bus); 						\
		atomic_thread_fence(memory_order_release);		\
		OSWriteLittleInt32((reg), 0, (value));			\
	})
#define ENA_REG_WRITE32_RELAXED(bus, value, reg)			\
	({ 								\
		(void)(bus); 						\
		OSWriteLittleInt32((reg), 0, (value));			\
	})
#define ENA_REG_READ32(bus, reg)					\
	({			 					\
		(void)(bus); 						\
		UInt32 value;						\
		atomic_thread_fence(memory_order_release);		\
		value = OSReadLittleInt32((reg), 0); 			\
		atomic_thread_fence(memory_order_acquire);		\
		(value);						\
	})

/* return value definition */
enum {
	ENA_COM_OK		= 0,
	ENA_COM_FAULT		= 1,
	ENA_COM_INVAL		= 2,
	ENA_COM_NO_MEM		= 3,
	ENA_COM_NO_SPACE	= 4,
	ENA_COM_TRY_AGAIN	= 5,
	ENA_COM_NO_DEVICE	= 6,
	ENA_COM_PERMISSION	= 7,
	ENA_COM_UNSUPPORTED	= 8,
	ENA_COM_TIMER_EXPIRED	= 9,
};

typedef IOSimpleLock * ena_spinlock_t;

/* Spinlock related methods */
#define ENA_SPINLOCK_INIT(spinlock) ((spinlock) = IOSimpleLockAlloc())
#define ENA_SPINLOCK_DESTROY(spinlock) IOSimpleLockFree((spinlock))
#define ENA_SPINLOCK_LOCK(spinlock, flags)				\
	do {								\
		(void)(flags);						\
		IOSimpleLockLock((spinlock));				\
	} while (0)
#define ENA_SPINLOCK_UNLOCK(spinlock, flags)				\
	do {								\
		(void)(flags);						\
		IOSimpleLockUnlock((spinlock));				\
	} while (0)

/* Wait queue related methods */
typedef IOLock * ena_wait_event_t;

#define ENA_WAIT_EVENT_INIT(wait_event) ((wait_event) = IOLockAlloc())
#define ENA_WAIT_EVENTS_DESTROY(admin_queue)				\
	do {								\
		struct ena_comp_ctx *comp_ctx;				\
		int i;							\
		for (i = 0; i < admin_queue->q_depth; i++) {		\
			comp_ctx = get_comp_ctxt(admin_queue, i, false); \
			if (comp_ctx != NULL)				\
				IOLockFree(comp_ctx->wait_event);	\
		}							\
	} while (0)
#define ENA_WAIT_EVENT_CLEAR(wait_event) IOLockWakeup(wait_event, wait_event, false)
#define ENA_WAIT_EVENT_WAIT(wait_event, timeout_us)			\
	do {								\
		AbsoluteTime time;					\
		nanoseconds_to_absolutetime((timeout_us)*1000, &time);	\
		time += mach_absolute_time();				\
		IOLockLock(wait_event);					\
		IOLockSleepDeadline(wait_event, wait_event, time, THREAD_UNINT); \
		IOLockUnlock(wait_event);				\
	} while (0)
#define ENA_WAIT_EVENT_SIGNAL(wait_event) 				\
	IOLockWakeup(wait_event, wait_event, true)

#define ENA_MEMCPY_TO_DEVICE_64(dst, src, size)				\
	do {								\
		int offset;						\
		const UInt64 *from = (const UInt64 *)(src);		\
		for (offset = 0; offset < (size); offset += 8, ++from)	\
			_OSWriteInt64((dst), offset, *from);		\
	} while (0)

#define ENA_MEM_ALLOC_COHERENT_NODE(dmadev, size, virt, phys, handle, node, dev_node) \
	do {								\
		(virt) = NULL;						\
		(void)(dev_node);					\
	} while (0)
#define ENA_MEM_ALLOC_COHERENT(dmadev, size, virt, phys, handle)	\
	((virt) = ena_mem_alloc_coherent(size, &(phys), &(handle)))
#define ENA_MEM_FREE_COHERENT(dmadev, size, virt, phys, handle)		\
	do {								\
		(void)(size);						\
		ena_mem_free_coherent(handle);				\
	} while (0)

#define ENA_MEM_ALLOC(dmadev, size)	\
	({	\
		void *addr = IOMalloc(size);	\
		memset(addr, 0, size);	\
		addr;	\
	})
#define ENA_MEM_ALLOC_NODE(dmadev, size, virt, node, dev_node)		\
	((virt) = NULL)
#define ENA_MEM_FREE(dmadev, ptr, size)					\
	{ (void)(dmadev); IOFree((ptr), (size)); }
#define ENA_DB_SYNC(mem_handle) ((void)(mem_handle))

typedef SInt32 ena_atomic32_t;
#define ATOMIC32_INC(I32_PTR) OSIncrementAtomic(I32_PTR)
#define ATOMIC32_DEC(I32_PTR) OSDecrementAtomic(I32_PTR)
#define ATOMIC32_READ(I32_PTR) (*(volatile ena_atomic32_t *)(I32_PTR))
#define ATOMIC32_SET(I32_PTR, VAL) 					\
	(*(volatile ena_atomic32_t *)(I32_PTR) = (VAL))

#define GENMASK(h, l)	(((~0U) - (1U << (l)) + 1) & (~0U >> (32 - 1 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (64 - 1 - (h))))
#define BIT(x)		(1 << (x))

#define SZ_256			(256)
#define SZ_4K			(4096)

typedef UInt8	u8;
typedef UInt16	u16;
typedef UInt32	u32;
typedef UInt64	u64;

typedef AbsoluteTime ena_time_t;

typedef IOPhysicalAddress dma_addr_t;
typedef void * ena_mem_handle_t;

ENA_COM_API
void * ena_mem_alloc_coherent(size_t length,
	IOPhysicalAddress *physAddr, ena_mem_handle_t *memHandle);

ENA_COM_API
void ena_mem_free_coherent(ena_mem_handle_t memHandle);

#define __iomem
#define ____cacheline_aligned __attribute__((aligned(64)))

#define unlikely(x)		__builtin_expect(!!(x), 0)
#define likely(x)		__builtin_expect(!!(x), 1)

#define prefetch(x)		__builtin_prefetch(x)
#define prefetchw(x)		__builtin_prefetch(x, 1)

#if	defined(__arm__) || defined(__arm64__)
static inline void barrier(void) {
	__asm__ volatile("dmb ish" ::: "memory");
}
#elif defined(__i386__) || defined(__x86_64__)
static inline void barrier(void) {
	__asm__ volatile("mfence" ::: "memory");
}
#endif

#define mb()		atomic_thread_fence(memory_order_acq_rel)
#define wmb()		atomic_thread_fence(memory_order_release)
#define rmb()		atomic_thread_fence(memory_order_acquire)
#define dma_rmb		rmb
#define mmiowb		wmb

#define	ACCESS_ONCE(x) (*(volatile __typeof(x) *)&(x))
#define READ_ONCE(x) ({							\
			__typeof(x) __var;				\
			barrier();					\
			__var = ACCESS_ONCE(x);				\
			barrier();					\
			__var;						\
		})
#define READ_ONCE8(x) READ_ONCE(x)
#define READ_ONCE16(x) READ_ONCE(x)
#define READ_ONCE32(x) READ_ONCE(x)

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) unlikely((x) <= (unsigned long)MAX_ERRNO)

static inline long IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

static inline void *ERR_PTR(long error)
{
	return (void *)error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((u32)(n))

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#include "ena_defs/ena_includes.h"

#define ENA_FFS(x) ffs(x)
/* RSS is not used in the driver right now, as OS limit for the number of the IO
 * queues is 1. It needs to be implemented after multiqueue support will be
 * added.
 * As for now, not having RSS key configured at all won't change anything in
 * the driver's functionality.
 */
#define ENA_RSS_FILL_KEY(key, size)

#endif /* ENA_COM_ENA_PLAT_MACOS_H_ */
