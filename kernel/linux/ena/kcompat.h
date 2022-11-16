/*******************************************************************************
Modified by Amazon 2015-2016.
Copyright 2015-2016, Amazon.com, Inc. or its affiliates. All Rights Reserved.

Modifications subject to the terms and conditions of the GNU General
Public License, version 2.
*******************************************************************************/

/*******************************************************************************

Intel 10 Gigabit PCI Express Linux driver
Copyright(c) 1999 - 2013 Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

The full GNU General Public License is included in this distribution in
the file called "COPYING".

Contact Information:
e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#include <asm/io.h>

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33) )
#include <linux/utsrelease.h>
#else
#include <generated/utsrelease.h>
#endif

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/ip.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/udp.h>
#include <linux/u64_stats_sync.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#include <linux/sizes.h>
#endif

/* For ACCESS_ONCE, WRITE_ONCE and READ_ONCE macros */
#include<linux/compiler.h>

#ifndef SZ_256
#define SZ_256 0x0000100
#endif

#ifndef SZ_4K
#define SZ_4K 0x00001000
#endif

#ifndef SZ_16K
#define SZ_16K 0x00004000
#endif

#ifdef HAVE_POLL_CONTROLLER
#define CONFIG_NET_POLL_CONTROLLER
#endif

#ifndef __GFP_COLD
#define __GFP_COLD 0
#endif

#if defined(CONFIG_NET_RX_BUSY_POLL) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0) && \
	LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#define ENA_BUSY_POLL_SUPPORT
#endif
/******************************************************************************/
/************************** Ubuntu macros *************************************/
/******************************************************************************/

/* Ubuntu Release ABI is the 4th digit of their kernel version. You can find
 * it in /usr/src/linux/$(uname -r)/include/generated/utsrelease.h for new
 * enough versions of Ubuntu. Otherwise you can simply see it in the output of
 * uname as the 4th digit of the kernel. The UTS_UBUNTU_RELEASE_ABI is not in
 * the linux-source package, but in the linux-headers package. It begins to
 * appear in later releases of 14.04 and 14.10.
 *
 * Ex:
 * <Ubuntu 14.04.1>
 *  $uname -r
 *  3.13.0-45-generic
 * ABI is 45
 *
 * <Ubuntu 14.10>
 *  $uname -r
 *  3.16.0-23-generic
 * ABI is 23
 */
#ifdef UTS_UBUNTU_RELEASE_ABI

#if UTS_UBUNTU_RELEASE_ABI > 255
#undef UTS_UBUNTU_RELEASE_ABI
#define UTS_UBUNTU_RELEASE_ABI 0
#endif /* UTS_UBUNTU_RELEASE_ABI > 255 */

/* Ubuntu does not provide actual release version macro, so we use the kernel
 * version plus the ABI to generate a unique version code specific to Ubuntu.
 * In addition, we mask the lower 8 bits of LINUX_VERSION_CODE in order to
 * ignore differences in sublevel which are not important since we have the
 * ABI value. Otherwise, it becomes impossible to correlate ABI to version for
 * ordering checks.
 */
#define UBUNTU_VERSION_CODE (((LINUX_VERSION_CODE & ~0xFF) << 8) + (UTS_UBUNTU_RELEASE_ABI))

#endif /* UTS_UBUNTU_RELEASE_ABI */

/* Note that the 3rd digit is always zero, and will be ignored. This is
 * because Ubuntu kernels are based on x.y.0-ABI values, and while their linux
 * version codes are 3 digit, this 3rd digit is superseded by the ABI value.
 */
#define UBUNTU_VERSION(a,b,c,d) ((KERNEL_VERSION(a,b,0) << 8) + (d))

/******************************************************************************/
/**************************** SuSE macros *************************************/
/******************************************************************************/

/* SuSE version macro is the same as Linux kernel version */
#ifndef SLE_VERSION
#define SLE_VERSION(a,b,c) KERNEL_VERSION(a,b,c)
#endif
#ifdef CONFIG_SUSE_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 14)
#include <linux/suse_version.h>
#endif
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,28) )
/* SLES12 is at least 3.12.28+ based */
#define SLE_VERSION_CODE SLE_VERSION(12,0,0)
#endif /* LINUX_VERSION_CODE == KERNEL_VERSION(x,y,z) */
#endif /* CONFIG_SUSE_KERNEL */
#ifndef SLE_VERSION_CODE
#define SLE_VERSION_CODE 0
#endif /* SLE_VERSION_CODE */
#ifndef SUSE_VERSION
#define SUSE_VERSION 0
#endif /* SUSE_VERSION */


/******************************************************************************/
/**************************** RHEL macros *************************************/
/******************************************************************************/

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a,b) (((a) << 8) + (b))
#endif
#ifndef AX_RELEASE_VERSION
#define AX_RELEASE_VERSION(a,b) (((a) << 8) + (b))
#endif

#ifndef AX_RELEASE_CODE
#define AX_RELEASE_CODE 0
#endif

#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif

#if (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3,0))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5,0)
#elif (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3,1))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5,1)
#elif (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3,2))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5,3)
#endif

/*****************************************************************************/
#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,6)) && \
     (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0)))
#define HAVE_RHEL6_NET_DEVICE_OPS_EXT
#endif

#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,4)) && \
     (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0)))
#define HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
#endif /* RHEL >= 6.4 && RHEL < 7.0 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0) || \
	(SLE_VERSION_CODE && \
	 LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,48)))
#define HAVE_MTU_MIN_MAX_IN_NET_DEVICE
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0) || \
     (RHEL_RELEASE_CODE && \
      RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,5)) || \
     (SLE_VERSION_CODE && \
      LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,50)))
#define NDO_GET_STATS_64_V2
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0) || \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,5))
#include <net/busy_poll.h>
#endif

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37) )
/* The function netif_set_real_num_tx_queues() doesn't return value for
 * kernels < 2.6.37
 */
static inline int _kc_netif_set_real_num_tx_queues(struct net_device *dev,
                                                   unsigned int txq)
{
        netif_set_real_num_tx_queues(dev, txq);
        return 0;
}
#define netif_set_real_num_tx_queues(dev, txq) \
        _kc_netif_set_real_num_tx_queues(dev, txq)

#endif /* < 2.6.37 */

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0) )
#if !(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,5))
typedef u32 netdev_features_t;
#endif
#undef PCI_EXP_TYPE_RC_EC
#define  PCI_EXP_TYPE_RC_EC	0xa	/* Root Complex Event Collector */
#ifndef CONFIG_BQL
#define netdev_tx_completed_queue(_q, _p, _b) do {} while (0)
#define netdev_completed_queue(_n, _p, _b) do {} while (0)
#define netdev_tx_sent_queue(_q, _b) do {} while (0)
#define netdev_sent_queue(_n, _b) do {} while (0)
#define netdev_tx_reset_queue(_q) do {} while (0)
#define netdev_reset_queue(_n) do {} while (0)
#endif

#endif /* < 3.3.0 */

/******************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0) )
#ifdef NET_ADDR_RANDOM
#define eth_hw_addr_random(N) do { \
	eth_random_addr(N->dev_addr); \
	N->addr_assign_type |= NET_ADDR_RANDOM; \
	} while (0)
#else /* NET_ADDR_RANDOM */
#define eth_hw_addr_random(N) eth_random_addr(N->dev_addr)
#endif /* NET_ADDR_RANDOM */
#if !(RHEL_RELEASE_CODE)
/* If probe retry doesn't define, return no device */
#define EPROBE_DEFER ENODEV
#endif
#endif /* >= 3.4.0 */

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) )
#if !(RHEL_RELEASE_CODE)
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	const u16 *a = (const u16 *)addr1;
	const u16 *b = (const u16 *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
}
#endif
#endif /* >= 3.5.0 */

/******************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0) )
#ifndef eth_random_addr
#define eth_random_addr _kc_eth_random_addr
static inline void _kc_eth_random_addr(u8 *addr)
{
        get_random_bytes(addr, ETH_ALEN);
        addr[0] &= 0xfe; /* clear multicast */
        addr[0] |= 0x02; /* set local assignment */
}
#endif
#endif /* < 3.6.0 */

/******************************************************************************/
#ifndef CONFIG_NET_RX_BUSY_POLL
static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{

}

static inline void napi_hash_del(struct napi_struct *napi)
{

}

static inline void napi_hash_add(struct napi_struct *napi)
{

}
#endif /* CONFIG_NET_RX_BUSY_POLL */

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0) )
/* cpu_rmap is buggy on older version and causes dead lock */
#ifdef CONFIG_RFS_ACCEL
#undef CONFIG_RFS_ACCEL
#endif

#if !(RHEL_RELEASE_CODE)
static inline u32 ethtool_rxfh_indir_default(u32 index, u32 n_rx_rings)
{
	return index % n_rx_rings;
}
#endif
#endif /* >= 3.8.0 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK_V3
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)) || \
      (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,0))) || \
      (SUSE_VERSION && ((SUSE_VERSION == 12 && SUSE_PATCHLEVEL >= 5) || \
		        (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 1) || \
			(SUSE_VERSION > 15)))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK_V2
#else

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0) && \
      RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(7,2))) || \
     (LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0) && \
      SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) || \
     (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK_V1
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
#if defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,13,0,24)
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK_V1
#else
#define HAVE_NDO_SELECT_QUEUE_ACCEL
#endif
#endif /* >= 3.13 */
#endif /* < 4.19 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
# define u64_stats_init(syncp)  seqcount_init(syncp.seq)
#else
# define u64_stats_init(syncp)  do { } while (0)
#endif

#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) && \
	!(RHEL_RELEASE_CODE && ((RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,8) && \
	                        (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0))) \
                            || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,1)))) && \
     !defined(UEK3_RELEASE)
static inline void reinit_completion(struct completion *x)
{
         x->done = 0;
}
#endif /* SLE 12 */

#endif /* < 3.13.0 */

#if  (( LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0) ) && \
     (!(RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,0) && \
       RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7,0))) \
     && !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0))&& \
     !defined(UEK3_RELEASE))) || \
     (defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE < UBUNTU_VERSION(3,13,0,30))
static inline int pci_enable_msix_range(struct pci_dev *dev,
					struct msix_entry *entries,
					int minvec,
					int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msix(dev, entries, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0) && \
    !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(7,1))
static inline void *devm_kcalloc(struct device *dev,
				 size_t n, size_t size, gfp_t flags)
{
	return devm_kzalloc(dev, n * size, flags | __GFP_ZERO);
}
#endif

/*****************************************************************************/
#if (( LINUX_VERSION_CODE < KERNEL_VERSION(3,13,8) ) && \
     !RHEL_RELEASE_CODE && \
     !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0))) || \
     (defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE < UBUNTU_VERSION(3,13,0,30))
enum pkt_hash_types {
	PKT_HASH_TYPE_NONE,	/* Undefined type */
	PKT_HASH_TYPE_L2,	/* Input: src_MAC, dest_MAC */
	PKT_HASH_TYPE_L3,	/* Input: src_IP, dst_IP */
	PKT_HASH_TYPE_L4,	/* Input: src_IP, dst_IP, src_port, dst_port */
};

static inline void skb_set_hash(struct sk_buff *skb, __u32 hash,
	enum pkt_hash_types type)
{
	skb->l4_rxhash = (type == PKT_HASH_TYPE_L4);
	skb->rxhash = hash;
}
#endif

/*****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#if !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7,0) && \
			        RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(6,6)) \
    && !(defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,13,0,105))
static inline int pci_msix_vec_count(struct pci_dev *dev)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(dev, pos + PCI_MSIX_FLAGS, &control);
	return (control & 0x7FF) + 1;
}
#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) && \
    !(RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(7,0))
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
	memcpy(dst, src, 6);
}
#endif /* SLE 12 */
#endif /* RHEL 7 */
#endif

#if (RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6,8)))
#define napi_gro_flush(napi, flush_old) napi_gro_flush(napi)
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0) || \
	(defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,13,0,30))) || \
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7,0) \
	                     && RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7,1))
#else
static inline bool u64_stats_fetch_retry_irq(const struct u64_stats_sync *syncp,
					     unsigned int start)
{
	return u64_stats_fetch_retry(syncp, start);
}

static inline unsigned int u64_stats_fetch_begin_irq(const struct u64_stats_sync *syncp)
{
	return u64_stats_fetch_begin(syncp);
}

#endif

static inline bool ena_u64_stats_fetch_retry(const struct u64_stats_sync *syncp,
					     unsigned int start)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return u64_stats_fetch_retry_irq(syncp, start);
#else
	return u64_stats_fetch_retry(syncp, start);
#endif
}

static inline unsigned int ena_u64_stats_fetch_begin(const struct u64_stats_sync *syncp)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return u64_stats_fetch_begin_irq(syncp);
#else
	return u64_stats_fetch_begin(syncp);
#endif
}

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0) && \
      !(RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,1))))

#define smp_mb__before_atomic()	smp_mb()

#endif

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0) )
#undef GENMASK
#define GENMASK(h, l)	(((U32_C(1) << ((h) - (l) + 1)) - 1) << (l))
#undef GENMASK_ULL
#define GENMASK_ULL(h, l) (((U64_C(1) << ((h) - (l) + 1)) - 1) << (l))
#endif
/*****************************************************************************/

#ifndef dma_rmb
#define dma_rmb rmb
#endif

#ifndef writel_relaxed
#define writel_relaxed writel
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) ) \
	|| (SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) \
	|| (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7,0))
#else
static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	get_random_bytes(buffer, len);
}
#endif

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) ) && \
    !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) && \
    !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,2))

static inline void napi_schedule_irqoff(struct napi_struct *n)
{
	napi_schedule(n);
}

static inline void __napi_schedule_irqoff(struct napi_struct *n)
{
	__napi_schedule(n);
}

#ifndef READ_ONCE
#define READ_ONCE(var) (*((volatile typeof(var) *)(&(var))))
#endif
#endif /* Kernel 3.19 */

/*****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0) \
	|| (RHEL_RELEASE_CODE && ((RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6,7)) && \
	                          (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0))) \
	                      || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,2)) \
	|| (defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,19,0,51))
#else
static inline void napi_complete_done(struct napi_struct *n, int work_done)
{
	napi_complete(n);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0) \
	|| (defined(UBUNTU_VERSION_CODE) && \
	(UBUNTU_VERSION(3,13,0,126) <= UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE < UBUNTU_VERSION(3,14,0,0))) \
	|| (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,5))

#else

static inline void ioremap_release(struct device *dev, void *res)
{
	iounmap(*(void __iomem **)res);
}


static inline void __iomem *devm_ioremap_wc(struct device *dev,
					    resource_size_t offset,
					    resource_size_t size)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioremap_wc(offset, size);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
#endif

#if RHEL_RELEASE_CODE && \
    RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define ndo_change_mtu ndo_change_mtu_rh74
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
#ifndef dma_zalloc_coherent
#define dma_zalloc_coherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#endif
#endif

#ifndef dev_info_once
#ifdef CONFIG_PRINTK
#define dev_info_once(dev, fmt, ...)			\
do {									\
	static bool __print_once __read_mostly;				\
									\
	if (!__print_once) {						\
		__print_once = true;					\
		dev_info(dev, fmt, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define dev_info_once(dev, fmt, ...)			\
do {									\
	if (0)								\
		dev_info(dev, fmt, ##__VA_ARGS__);			\
} while (0)
#endif
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)) || \
	(RHEL_RELEASE_CODE && \
	RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 2))
#define HAVE_NETDEV_XMIT_MORE
#endif

#ifndef mmiowb
#define MMIOWB_NOT_DEFINED
#endif

/* In the driver we currently only support CRC32 and Toeplitz.
 * Since in kernel erlier than 4.12 the CRC32 define didn't exist
 * We define it here to be XOR. Any user who wishes to select CRC32
 * as the hash function, can do so by choosing xor through ethtool.
 */
#ifndef ETH_RSS_HASH_CRC32
#define ETH_RSS_HASH_CRC32 ETH_RSS_HASH_XOR
#endif

#ifndef _ULL
#define _ULL(x) (_AC(x, ULL))
#endif

#ifndef ULL
#define ULL(x) (_ULL(x))
#endif

#ifndef BIT_ULL
#define BIT_ULL(nr) (ULL(1) << (nr))
#endif

#ifndef BITS_PER_TYPE
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)
#endif

#ifndef DIV_ROUND_DOWN_ULL
#define DIV_ROUND_DOWN_ULL(ll, d) \
	({ unsigned long long _tmp = (ll); do_div(_tmp, d); _tmp; })
#endif

/* values are taken from here: https://github.com/iovisor/bcc/blob/master/docs/kernel-versions.md */

#if defined(CONFIG_BPF) && LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
#define ENA_XDP_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)) || \
	(SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 3)
#define XDP_HAS_FRAME_SZ
#define XDP_CONVERT_TO_FRAME_NAME_CHANGED
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
#define ENA_XDP_QUERY_IN_KERNEL
#endif

#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0) || \
	(defined(RHEL_RELEASE_CODE) && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 3))) || \
	(SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 3)

#define HAVE_NDO_TX_TIMEOUT_STUCK_QUEUE_PARAMETER
#endif

#if defined(CONFIG_NET_DEVLINK) && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#define ENA_DEVLINK_SUPPORT
#endif

#if !defined(CONFIG_NET_DEVLINK) && !defined(CONFIG_NET_DEVLINK_MODULE) && !defined(CONFIG_MAY_USE_DEVLINK)
#define ENA_NO_DEVLINK_HEADERS
#endif

#if defined(CONFIG_NET_DEVLINK) &&					\
	(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0) ||		\
	 (SUSE_VERSION && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 18)))
#define ENA_DEVLINK_RELOAD_UP_DOWN_SUPPORTED
#endif

#if defined(CONFIG_NET_DEVLINK) && \
	(KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE && LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) && \
	!(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4)) && \
	!(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 0)))
#define ENA_DEVLINK_RELOAD_ENABLING_REQUIRED
#endif

#if defined(CONFIG_NET_DEVLINK) &&					\
	(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0) ||		\
	 (SUSE_VERSION && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 18)))
#define ENA_DEVLINK_RELOAD_NS_CHANGE_SUPPORT
#endif

#if defined(CONFIG_NET_DEVLINK) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#define ENA_DEVLINK_RELOAD_LIMIT_AND_ACTION_SUPPORT
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) || \
	(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4)) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 0))
#define ENA_DEVLINK_RECEIVES_DEVICE_ON_ALLOC
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0) || \
	(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4))
#define ENA_DEVLINK_RELOAD_SUPPORT_ADVERTISEMENT_NEEDED
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) && \
	!(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4)) && \
	!(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 0))
#define ENA_DEVLINK_CONFIGURE_AFTER_REGISTER
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) && \
    !(RHEL_RELEASE_CODE && ((RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7, 1)) && \
			    (RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6, 6)))) && \
			    !defined(UBUNTU_VERSION_CODE) && \
			    !defined(UEK3_RELEASE) && (!defined(DEBIAN_VERSION) || DEBIAN_VERSION != 8)

#define DO_ONCE(func, ...)						     \
	({								     \
		static bool ___done = false;				     \
		if (unlikely(!___done)) {				     \
				func(__VA_ARGS__);			     \
				___done = true;				     \
		}							     \
	})

#define get_random_once(buf, nbytes)					     \
	DO_ONCE(get_random_bytes, (buf), (nbytes))

#define net_get_random_once(buf, nbytes)				     \
	get_random_once((buf), (nbytes))

/* RSS keys are 40 or 52 bytes long */
#define NETDEV_RSS_KEY_LEN 52
static u8 netdev_rss_key[NETDEV_RSS_KEY_LEN];

static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	BUG_ON(len > sizeof(netdev_rss_key));
	net_get_random_once(netdev_rss_key, sizeof(netdev_rss_key));
	memcpy(buffer, netdev_rss_key, len);
}
#endif

#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) (ACCESS_ONCE(x) = val)
#endif
#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9 ,0)
#define ENA_GENERIC_PM_OPS
#endif

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(4, 6 ,0)) && \
     !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,3)))
/* Linux versions 4.4.216 - 4.5 (non inclusive) back propagated page_ref_count
 * function from kernel 4.6. To make things more difficult, Ubuntu didn't add
 * these changes to its 4.4.* kernels
 */
#if !(KERNEL_VERSION(4, 4 ,216) <= LINUX_VERSION_CODE && LINUX_VERSION_CODE < KERNEL_VERSION(4, 5 ,0)) ||\
      defined(UBUNTU_VERSION_CODE)
static inline int page_ref_count(struct page *page)
{
	return atomic_read(&page->_count);
}
#endif /* !(KERNEL_VERSION(4, 4 ,216) <= LINUX_VERSION_CODE && LINUX_VERSION_CODE < KERNEL_VERSION(4, 5 ,0)) */

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_count);
}
#endif

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)) && \
     !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,2)))
static inline struct page *dev_alloc_page(void)
{
	gfp_t gfp_mask = GFP_ATOMIC | __GFP_NOWARN;

	gfp_mask |= __GFP_COLD | __GFP_COMP;

	return alloc_pages_node(NUMA_NO_NODE, gfp_mask, 0);
}
#endif

/* This entry might seem strange because of the #ifndef numa_mem_id(),
 * but these defines were taken from the Linux kernel
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
#ifndef numa_mem_id
#ifdef CONFIG_HAVE_MEMORYLESS_NODES
static inline int numa_mem_id(void)
{
	return __this_cpu_read(_numa_mem_);
}
#else /* CONFIG_HAVE_MEMORYLESS_NODES */
static inline int numa_mem_id(void)
{
	return numa_node_id();
}
#endif /* CONFIG_HAVE_MEMORYLESS_NODES */
#endif /* numa_mem_id */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34) */

#ifndef fallthrough
#define fallthrough do {} while (0)  /* fallthrough */
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#define AF_XDP_BUSY_POLL_SUPPORTED
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
#define ENA_LINEAR_FRAG_SUPPORTED
static __always_inline struct sk_buff*
ena_build_skb(void *data, unsigned int frag_size)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	return napi_build_skb(data, frag_size);
#else
	return build_skb(data, frag_size);
#endif
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0) && \
	!(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)) && \
	!(defined(UBUNTU_VERSION_CODE) && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(4, 2, 0, 42))
static __always_inline
void napi_consume_skb(struct sk_buff *skb, int budget)
{
	dev_kfree_skb_any(skb);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#define ENA_NETDEV_LOGS_WITHOUT_RV
#endif

#if defined(ENA_XDP_SUPPORT) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
static __always_inline void
xdp_init_buff(struct xdp_buff *xdp, u32 frame_sz, struct xdp_rxq_info *rxq)
{
	xdp->rxq = rxq;
#ifdef XDP_HAS_FRAME_SZ
	xdp->frame_sz = frame_sz;
#endif
}

static __always_inline void
xdp_prepare_buff(struct xdp_buff *xdp, unsigned char *hard_start,
		 int headroom, int data_len, const bool meta_valid)
{
	unsigned char *data = hard_start + headroom;

	xdp->data_hard_start = hard_start;
	xdp->data = data;
	xdp->data_end = data + data_len;
	xdp->data_meta = meta_valid ? data : data + 1;
}

#endif /* defined(ENA_XDP_SUPPORT) && LINUX_VERSION_CODE <= KERNEL_VERSION(5, 12, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
#define ethtool_sprintf(data, fmt, args...)			\
	do {							\
		snprintf(*data, ETH_GSTRING_LEN, fmt, ##args);	\
		(*data) += ETH_GSTRING_LEN;			\
	} while(0)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)
#define ENA_XDP_XMIT_FREES_FAILED_DESCS_INTERNALLY
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0) && \
	!(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 6)) && \
	!(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4))
static inline void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
	memcpy(dev->dev_addr, addr, ETH_ALEN);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) || \
	(defined(RHEL_RELEASE_CODE) && \
	RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 6) && \
	RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9, 0)) || \
	(defined(SUSE_VERSION) && (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 4))
#define ENA_EXTENDED_COALESCE_UAPI_WITH_CQE_SUPPORTED
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(8, 7))
#define ENA_ETHTOOL_RX_BUFF_SIZE_CHANGE
#endif

#if defined(ENA_XDP_SUPPORT) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#define ENA_AF_XDP_SUPPORT
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
/* kernels older than 3.3.0 didn't have this function and
 * used netif_tx_queue_stopped() for the same purpose
 */
static inline int netif_xmit_stopped(const struct netdev_queue *dev_queue)
{
	return netif_tx_queue_stopped(dev_queue);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#define NAPIF_STATE_SCHED BIT(NAPI_STATE_SCHED)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
#define bpf_warn_invalid_xdp_action(netdev, xdp_prog, verdict) \
	bpf_warn_invalid_xdp_action(verdict)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
#define HAS_BPF_HEADER
#endif

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && \
	!(RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 7))))
static inline int ktime_compare(const ktime_t cmp1, const ktime_t cmp2)
{
	if (cmp1.tv64 < cmp2.tv64)
		return -1;
	if (cmp1.tv64 > cmp2.tv64)
		return 1;
	return 0;
}
#endif

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)) && \
	!(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 7)) && \
	(RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7, 0)) && \
	(RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(7, 1))))
static inline bool ktime_after(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) > 0;
}
#endif

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)

#if defined(ENA_PHC_INCLUDE) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 4)))
#define ENA_PHC_SUPPORT
#endif /* ENA_PHC_SUPPORT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 2))
#define ENA_PHC_SUPPORT_GETTIME64
#endif /* ENA_PHC_SUPPORT_GETTIME64 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)) || \
	(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 7)) && \
	(RHEL_RELEASE_CODE != RHEL_RELEASE_VERSION(8, 0)))
#define ENA_PHC_SUPPORT_GETTIME64_EXTENDED
#endif /* ENA_PHC_SUPPORT_GETTIME64_EXTENDED */

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)) && \
	!(RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 4))))
#define ptp_clock_register(info, parent) ptp_clock_register(info)
#endif

#endif /* CONFIG_PTP_1588_CLOCK */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)) && \
	!(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 2)))
static inline struct sk_buff *napi_alloc_skb(struct napi_struct *napi,
					     unsigned int length)
{
	return netdev_alloc_skb_ip_align(napi->dev, length);
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)) && \
	!(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 7)))
static inline ssize_t strscpy(char *dest, const char *src, size_t count)
{
	return (ssize_t)strlcpy(dest, src, count);
}
#endif

static inline void ena_netif_napi_add(struct net_device *dev,
				      struct napi_struct *napi,
				      int (*poll)(struct napi_struct *, int))
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
#ifndef NAPI_POLL_WEIGHT
#define NAPI_POLL_WEIGHT 64
#endif
	netif_napi_add(dev, napi, poll, NAPI_POLL_WEIGHT);
#else
	netif_napi_add(dev, napi, poll);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0) */
}

#endif /* _KCOMPAT_H_ */
