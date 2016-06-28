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
#else
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#include <asm/io.h>

#include <generated/utsrelease.h>

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#include <linux/sizes.h>
#endif
#ifndef SZ_4K
#define SZ_4K 0x00001000
#endif

#ifndef SZ_256
#define SZ_256 0x0000100
#endif

#ifdef HAVE_POLL_CONTROLLER
#define CONFIG_NET_POLL_CONTROLLER
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
#ifndef UTS_UBUNTU_RELEASE_ABI
#define UTS_UBUNTU_RELEASE_ABI 0
#define UBUNTU_VERSION_CODE 0
#else
/* Ubuntu does not provide actual release version macro, so we use the kernel
 * version plus the ABI to generate a unique version code specific to Ubuntu.
 * In addition, we mask the lower 8 bits of LINUX_VERSION_CODE in order to
 * ignore differences in sublevel which are not important since we have the
 * ABI value. Otherwise, it becomes impossible to correlate ABI to version for
 * ordering checks.
 */

#define UBUNTU_VERSION_CODE (((LINUX_VERSION_CODE & ~0xFF) << 8) + (UTS_UBUNTU_RELEASE_ABI))

#if UTS_UBUNTU_RELEASE_ABI > 255
#undef UTS_UBUNTU_RELEASE_ABI
#define UTS_UBUNTU_RELEASE_ABI 0
#endif /* UTS_UBUNTU_RELEASE_ABI > 255 */

#endif

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
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,28) )
/* SLES12 is at least 3.12.28+ based */
#define SLE_VERSION_CODE SLE_VERSION(12,0,0)
#endif /* LINUX_VERSION_CODE == KERNEL_VERSION(x,y,z) */
#endif /* CONFIG_SUSE_KERNEL */
#ifndef SLE_VERSION_CODE
#define SLE_VERSION_CODE 0
#endif /* SLE_VERSION_CODE */


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

struct dev_ext_attribute {
	struct device_attribute attr;
	void *var;
};
#endif /* < 3.3.0 */

/******************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0) )
#ifndef skb_add_rx_frag
#define skb_add_rx_frag _kc_skb_add_rx_frag
static inline void _kc_skb_add_rx_frag(struct sk_buff *skb, int i,
				       struct page *page, int off, int size,
				       unsigned int truesize)
{
	skb_fill_page_desc(skb, i, page, off, size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}
#endif
#ifdef NET_ADDR_RANDOM
#define eth_hw_addr_random(N) do { \
	eth_random_addr(N->dev_addr); \
	N->addr_assign_type |= NET_ADDR_RANDOM; \
	} while (0)
#else /* NET_ADDR_RANDOM */
#define eth_hw_addr_random(N) eth_random_addr(N->dev_addr)
#endif /* NET_ADDR_RANDOM */

/* If probe retry doesn't define, return no device */
#define EPROBE_DEFER ENODEV
#endif /* >= 3.4.0 */

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) )

static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	const u16 *a = (const u16 *)addr1;
	const u16 *b = (const u16 *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
}
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

/*****************************************************************************/
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0) )
/* cpu_rmap is buggy on older version and causes dead lock */
#ifdef CONFIG_RFS_ACCEL
#undef CONFIG_RFS_ACCEL
#endif

static inline u32 ethtool_rxfh_indir_default(u32 index, u32 n_rx_rings)
{
	return index % n_rx_rings;
}
#else /* >= 3.8.0 */
#ifndef HAVE_SRIOV_CONFIGURE
#define HAVE_SRIOV_CONFIGURE
#endif
#endif /* >= 3.8.0 */

/*****************************************************************************/
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0) )
#if ( SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif
#else
#define GENMASK(h, l)	(((U32_C(1) << ((h) - (l) + 1)) - 1) << (l))
#define GENMASK_ULL(h, l) (((U64_C(1) << ((h) - (l) + 1)) - 1) << (l))
#endif /* >= 3.12.0 */

/*****************************************************************************/
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0) )
#if (UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,13,0,24))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#else
#define HAVE_NDO_SELECT_QUEUE_ACCEL
#endif
#else

#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
# define u64_stats_init(syncp)  seqcount_init(syncp.seq)
#else
# define u64_stats_init(syncp)  do { } while (0)
#endif

#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) && \
	!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0))
static inline void reinit_completion(struct completion *x)
{
         x->done = 0;
}
#endif /* SLE 12 */
#endif /* >= 3.13.0 */

/*****************************************************************************/
#if (( LINUX_VERSION_CODE < KERNEL_VERSION(3,13,8) ) && \
     !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0)) && \
     !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)))
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
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0) )
/* for ndo_dfwd_ ops add_station, del_station and _start_xmit */
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#else
#if !(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0))
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
#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0))
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
	memcpy(dst, src, 6);
}
#endif /* SLE 12 */
#endif /* RHEL 7 */
#endif


#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0) || \
	(UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE > UBUNTU_VERSION(3,13,0,24))) || \
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0))
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

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) ) \
	|| (SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12,0,0)) \
	|| (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0))
#else
static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	get_random_bytes(buffer, len);
}
#endif /* Kernel 3.19 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0) \
	|| (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,0)) \
	|| (UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3,19,0,51))
#else
static inline void napi_complete_done(struct napi_struct *n, int work_done)
{
	napi_complete(n);
}
#endif

#endif /* _KCOMPAT_H_ */
