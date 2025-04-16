// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/net_tstamp.h>

#include "ena_netdev.h"
#include "ena_xdp.h"
#include "ena_phc.h"

struct ena_stats {
	char name[ETH_GSTRING_LEN];
	int stat_offset;
};

struct ena_hw_metrics {
	char name[ETH_GSTRING_LEN];
};

enum ena_stat_type {
	ENA_TX_STAT,
	ENA_RX_STAT,
};

#define ENA_STAT_ENA_COM_ADMIN_ENTRY(stat) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_com_stats_admin, stat) / sizeof(u64) \
}

#define ENA_STAT_ENA_COM_PHC_ENTRY(stat) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_com_stats_phc, stat) / sizeof(u64) \
}

#define ENA_STAT_ENTRY(stat, stat_type) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_stats_##stat_type, stat) / sizeof(u64) \
}

#define ENA_STAT_HW_ENTRY(stat, stat_type) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_admin_##stat_type, stat) / sizeof(u64) \
}

#define ENA_STAT_RX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, rx)

#define ENA_STAT_TX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, tx)

#define ENA_STAT_GLOBAL_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, dev)

#define ENA_STAT_ENI_ENTRY(stat) \
	ENA_STAT_HW_ENTRY(stat, eni_stats)

#define ENA_STAT_ENA_SRD_ENTRY(stat) \
	ENA_STAT_HW_ENTRY(stat, ena_srd_stats)

#define ENA_STAT_ENA_SRD_MODE_ENTRY(stat) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_admin_ena_srd_info, flags) / sizeof(u64) \
}

#define ENA_METRIC_ENI_ENTRY(stat) { \
	.name = #stat \
}

static const struct ena_stats ena_stats_global_strings[] = {
	ENA_STAT_GLOBAL_ENTRY(total_resets),
	ENA_STAT_GLOBAL_ENTRY(reset_fail),
	ENA_STAT_GLOBAL_ENTRY(tx_timeout),
	ENA_STAT_GLOBAL_ENTRY(wd_expired),
	ENA_STAT_GLOBAL_ENTRY(admin_q_pause),
	ENA_STAT_GLOBAL_ENTRY(bad_tx_req_id),
	ENA_STAT_GLOBAL_ENTRY(bad_rx_req_id),
	ENA_STAT_GLOBAL_ENTRY(bad_rx_desc_num),
	ENA_STAT_GLOBAL_ENTRY(missing_intr),
	ENA_STAT_GLOBAL_ENTRY(suspected_poll_starvation),
	ENA_STAT_GLOBAL_ENTRY(missing_tx_cmpl),
	ENA_STAT_GLOBAL_ENTRY(rx_desc_malformed),
	ENA_STAT_GLOBAL_ENTRY(tx_desc_malformed),
	ENA_STAT_GLOBAL_ENTRY(invalid_state),
	ENA_STAT_GLOBAL_ENTRY(os_netdev_wd),
	ENA_STAT_GLOBAL_ENTRY(missing_admin_interrupt),
	ENA_STAT_GLOBAL_ENTRY(admin_to),
	ENA_STAT_GLOBAL_ENTRY(device_request_reset),
	ENA_STAT_GLOBAL_ENTRY(missing_first_intr),
	ENA_STAT_GLOBAL_ENTRY(suspend),
	ENA_STAT_GLOBAL_ENTRY(resume),
	ENA_STAT_GLOBAL_ENTRY(interface_down),
	ENA_STAT_GLOBAL_ENTRY(interface_up),
};

/* A partial list of hw stats. Used when admin command
 * with type ENA_ADMIN_GET_STATS_TYPE_CUSTOMER_METRICS is not supported
 */
static const struct ena_stats ena_stats_eni_strings[] = {
	ENA_STAT_ENI_ENTRY(bw_in_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(bw_out_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(pps_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(conntrack_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(linklocal_allowance_exceeded),
};

static const struct ena_hw_metrics ena_hw_stats_strings[] = {
	ENA_METRIC_ENI_ENTRY(bw_in_allowance_exceeded),
	ENA_METRIC_ENI_ENTRY(bw_out_allowance_exceeded),
	ENA_METRIC_ENI_ENTRY(pps_allowance_exceeded),
	ENA_METRIC_ENI_ENTRY(conntrack_allowance_exceeded),
	ENA_METRIC_ENI_ENTRY(linklocal_allowance_exceeded),
	ENA_METRIC_ENI_ENTRY(conntrack_allowance_available),
};

static const struct ena_stats ena_srd_info_strings[] = {
	ENA_STAT_ENA_SRD_MODE_ENTRY(ena_srd_mode),
	ENA_STAT_ENA_SRD_ENTRY(ena_srd_tx_pkts),
	ENA_STAT_ENA_SRD_ENTRY(ena_srd_eligible_tx_pkts),
	ENA_STAT_ENA_SRD_ENTRY(ena_srd_rx_pkts),
	ENA_STAT_ENA_SRD_ENTRY(ena_srd_resource_utilization)
};

/* These stats will be printed as accumulated values on reset */
static const struct ena_stats ena_accum_stats_tx_strings[] = {
	ENA_STAT_TX_ENTRY(cnt),
	ENA_STAT_TX_ENTRY(bytes),
	ENA_STAT_TX_ENTRY(queue_stop),
	ENA_STAT_TX_ENTRY(queue_wakeup),
	ENA_STAT_TX_ENTRY(dma_mapping_err),
	ENA_STAT_TX_ENTRY(linearize),
	ENA_STAT_TX_ENTRY(linearize_failed),
	ENA_STAT_TX_ENTRY(napi_comp),
	ENA_STAT_TX_ENTRY(prepare_ctx_err),
	ENA_STAT_TX_ENTRY(bad_req_id),
	ENA_STAT_TX_ENTRY(llq_buffer_copy),
	ENA_STAT_TX_ENTRY(missed_tx),
#ifdef ENA_AF_XDP_SUPPORT
	ENA_STAT_TX_ENTRY(xsk_cnt),
	ENA_STAT_TX_ENTRY(xsk_bytes),
	ENA_STAT_TX_ENTRY(xsk_need_wakeup_set),
	ENA_STAT_TX_ENTRY(xsk_wakeup_request),
#endif /* ENA_AF_XDP_SUPPORT */
#ifdef ENA_XDP_MB_SUPPORT
	ENA_STAT_TX_ENTRY(xdp_frags_exceeded),
	ENA_STAT_TX_ENTRY(xdp_short_linear_part),
#endif /* ENA_XDP_MB_SUPPORT */
};

/* These stats will be printed per queue on reset */
static const struct ena_stats ena_per_q_stats_tx_strings[] = {
	ENA_STAT_TX_ENTRY(tx_poll),
	ENA_STAT_TX_ENTRY(doorbells),
	ENA_STAT_TX_ENTRY(unmask_interrupt),
};

static const struct ena_stats ena_stats_rx_strings[] = {
	ENA_STAT_RX_ENTRY(cnt),
	ENA_STAT_RX_ENTRY(bytes),
	ENA_STAT_RX_ENTRY(rx_copybreak_pkt),
	ENA_STAT_RX_ENTRY(csum_good),
	ENA_STAT_RX_ENTRY(refil_partial),
	ENA_STAT_RX_ENTRY(csum_bad),
	ENA_STAT_RX_ENTRY(page_alloc_fail),
	ENA_STAT_RX_ENTRY(skb_alloc_fail),
	ENA_STAT_RX_ENTRY(dma_mapping_err),
	ENA_STAT_RX_ENTRY(bad_desc_num),
#ifdef ENA_BUSY_POLL_SUPPORT
	ENA_STAT_RX_ENTRY(bp_yield),
	ENA_STAT_RX_ENTRY(bp_missed),
	ENA_STAT_RX_ENTRY(bp_cleaned),
#endif
	ENA_STAT_RX_ENTRY(bad_req_id),
	ENA_STAT_RX_ENTRY(empty_rx_ring),
	ENA_STAT_RX_ENTRY(csum_unchecked),
#ifdef ENA_XDP_SUPPORT
	ENA_STAT_RX_ENTRY(xdp_aborted),
	ENA_STAT_RX_ENTRY(xdp_drop),
	ENA_STAT_RX_ENTRY(xdp_pass),
	ENA_STAT_RX_ENTRY(xdp_tx),
	ENA_STAT_RX_ENTRY(xdp_invalid),
	ENA_STAT_RX_ENTRY(xdp_redirect),
#endif
	ENA_STAT_RX_ENTRY(lpc_warm_up),
	ENA_STAT_RX_ENTRY(lpc_full),
	ENA_STAT_RX_ENTRY(lpc_wrong_numa),
#ifdef ENA_AF_XDP_SUPPORT
	ENA_STAT_RX_ENTRY(xsk_need_wakeup_set),
	ENA_STAT_RX_ENTRY(zc_queue_pkt_copy),
#endif /* ENA_AF_XDP_SUPPORT */
};

static const struct ena_stats ena_stats_ena_com_admin_strings[] = {
	ENA_STAT_ENA_COM_ADMIN_ENTRY(aborted_cmd),
	ENA_STAT_ENA_COM_ADMIN_ENTRY(submitted_cmd),
	ENA_STAT_ENA_COM_ADMIN_ENTRY(completed_cmd),
	ENA_STAT_ENA_COM_ADMIN_ENTRY(out_of_space),
	ENA_STAT_ENA_COM_ADMIN_ENTRY(no_completion),
};

static const struct ena_stats ena_stats_ena_com_phc_strings[] = {
	ENA_STAT_ENA_COM_PHC_ENTRY(phc_cnt),
	ENA_STAT_ENA_COM_PHC_ENTRY(phc_exp),
	ENA_STAT_ENA_COM_PHC_ENTRY(phc_skp),
	ENA_STAT_ENA_COM_PHC_ENTRY(phc_err),
};

#define ENA_STATS_ARRAY_GLOBAL		ARRAY_SIZE(ena_stats_global_strings)
#define ENA_ACCUM_STATS_ARRAY_TX	ARRAY_SIZE(ena_accum_stats_tx_strings)
#define ENA_PER_Q_STATS_ARRAY_TX	ARRAY_SIZE(ena_per_q_stats_tx_strings)
#define ENA_STATS_ARRAY_RX		ARRAY_SIZE(ena_stats_rx_strings)
#define ENA_STATS_ARRAY_ENA_COM_ADMIN	ARRAY_SIZE(ena_stats_ena_com_admin_strings)
#define ENA_STATS_ARRAY_ENA_COM_PHC	ARRAY_SIZE(ena_stats_ena_com_phc_strings)
#define ENA_STATS_ARRAY_ENI		ARRAY_SIZE(ena_stats_eni_strings)
#define ENA_STATS_ARRAY_ENA_SRD		ARRAY_SIZE(ena_srd_info_strings)
#define ENA_METRICS_ARRAY_ENI		ARRAY_SIZE(ena_hw_stats_strings)

static const char ena_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define ENA_PRIV_FLAGS_LPC	BIT(0)
	"local_page_cache",
};

#define ENA_PRIV_FLAGS_NR ARRAY_SIZE(ena_priv_flags_strings)

static void ena_safe_update_stat(u64 *src, u64 *dst,
				 struct u64_stats_sync *syncp)
{
	unsigned int start;

	do {
		start = ena_u64_stats_fetch_begin(syncp);
		*(dst) = *src;
	} while (ena_u64_stats_fetch_retry(syncp, start));
}

static void ena_safe_accumulate_stat(u64 *src, u64 *dst,
				     struct u64_stats_sync *syncp)
{
	unsigned int start;
	u64 val;

	do {
		start = ena_u64_stats_fetch_begin(syncp);
		val = *src;
	} while (ena_u64_stats_fetch_retry(syncp, start));

	*(dst) += val;
}

static void ena_metrics_stats(struct ena_adapter *adapter, u64 **data)
{
	struct ena_com_dev *dev = adapter->ena_dev;
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	if (ena_com_get_cap(dev, ENA_ADMIN_CUSTOMER_METRICS)) {
		u32 supported_metrics_count;
		int len;

		supported_metrics_count = ena_com_get_customer_metric_count(dev);
		len = supported_metrics_count * sizeof(u64);

		/* Fill the data buffer, and advance its pointer */
		ena_com_get_customer_metrics(dev, (char *)(*data), len);
		(*data) += supported_metrics_count;

	} else if (ena_com_get_cap(dev, ENA_ADMIN_ENI_STATS)) {
		struct ena_admin_eni_stats eni_stats;

		ena_com_get_eni_stats(dev, &eni_stats);
		/* Updating regardless of rc - once we told ethtool how many stats we have
		 * it will print that much stats. We can't leave holes in the stats
		 */
		for (i = 0; i < ENA_STATS_ARRAY_ENI; i++) {
			ena_stats = &ena_stats_eni_strings[i];
			ptr = (u64 *)&eni_stats + ena_stats->stat_offset;
			**data = *ptr;
			(*data)++;
		}
	}

	if (ena_com_get_cap(dev, ENA_ADMIN_ENA_SRD_INFO)) {
		struct ena_admin_ena_srd_info ena_srd_info;

		ena_com_get_ena_srd_info(dev, &ena_srd_info);
		/* Get ENA SRD mode */
		ena_stats = &ena_srd_info_strings[0];
		ptr = (u64 *)&ena_srd_info + ena_stats->stat_offset;
		**data = *ptr;
		(*data)++;
		for (i = 1; i < ENA_STATS_ARRAY_ENA_SRD; i++) {
			ena_stats = &ena_srd_info_strings[i];
			/* Wrapped within an outer struct - need to accommodate an
			 * additional offset of the ENA SRD mode that was already processed
			 */
			ptr = (u64 *)&ena_srd_info + ena_stats->stat_offset + 1;
			**data = *ptr;
			(*data)++;
		}
	}
}

static void ena_dump_stats_for_queue(struct ena_ring *ring, int stats_count,
				     u64 **data, enum ena_stat_type stat_type,
				     const struct ena_stats *stats_strings)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < stats_count; i++) {
		ena_stats = &stats_strings[i];

		if (stat_type == ENA_TX_STAT)
			ptr = (u64 *)&ring->tx_stats + ena_stats->stat_offset;
		else
			ptr = (u64 *)&ring->rx_stats + ena_stats->stat_offset;

		ena_safe_update_stat(ptr, (*data)++, &ring->syncp);
	}
}

static void ena_accumulate_queues_for_stat(struct ena_adapter *adapter,
					   const struct ena_stats *ena_stats,
					   u32 queues_nr, u64 **data,
					   enum ena_stat_type stat_type)
{
	u64 *ptr, accumulated_sum = 0;
	struct ena_ring *ring;
	int i;

	for (i = 0; i < queues_nr; i++) {
		if (stat_type == ENA_TX_STAT) {
			ring = &adapter->tx_ring[i];
			ptr = (u64 *)&ring->tx_stats + ena_stats->stat_offset;
		} else {
			ring = &adapter->rx_ring[i];
			ptr = (u64 *)&ring->rx_stats + ena_stats->stat_offset;
		}

		ena_safe_accumulate_stat(ptr, &accumulated_sum,
					 &ring->syncp);
	}

	ena_safe_update_stat(&accumulated_sum, (*data)++,
			     &ring->syncp);
}

static void ena_get_queue_stats(struct ena_adapter *adapter, u64 *data,
				bool print_accumulated_queue_stats)
{
	u32 queues_nr = adapter->num_io_queues + adapter->xdp_num_queues;
	const struct ena_stats *ena_stats;
	struct ena_ring *ring;
	int i;

	if (print_accumulated_queue_stats) {
		for (i = 0; i < ENA_ACCUM_STATS_ARRAY_TX; i++) {
			ena_stats = &ena_accum_stats_tx_strings[i];

			ena_accumulate_queues_for_stat(adapter, ena_stats,
						       queues_nr, &data,
						       ENA_TX_STAT);
		}

		for (i = 0; i < ENA_STATS_ARRAY_RX; i++) {
			ena_stats = &ena_stats_rx_strings[i];

			ena_accumulate_queues_for_stat(adapter, ena_stats,
						       adapter->num_io_queues,
						       &data, ENA_RX_STAT);
		}
	}

	for (i = 0; i < queues_nr; i++) {
		/* Tx stats */
		ring = &adapter->tx_ring[i];

		ena_dump_stats_for_queue(ring,
					 ENA_PER_Q_STATS_ARRAY_TX,
					 &data, ENA_TX_STAT,
					 ena_per_q_stats_tx_strings);

		ena_dump_stats_for_queue(ring,
					 ENA_ACCUM_STATS_ARRAY_TX,
					 &data, ENA_TX_STAT,
					 ena_accum_stats_tx_strings);

		/* XDP TX queues don't have a RX queue counterpart */
		if (ENA_IS_XDP_INDEX(adapter, i))
			continue;

		/* Rx stats */
		ring = &adapter->rx_ring[i];

		ena_dump_stats_for_queue(ring,
					 ENA_STATS_ARRAY_RX,
					 &data, ENA_RX_STAT,
					 ena_stats_rx_strings);
	}
}

static void ena_get_admin_queue_stats(struct ena_adapter *adapter, u64 **data)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM_ADMIN; i++) {
		ena_stats = &ena_stats_ena_com_admin_strings[i];

		ptr = (u64 *)&adapter->ena_dev->admin_queue.stats +
			ena_stats->stat_offset;

		*(*data)++ = *ptr;
	}
}

static void ena_get_phc_stats(struct ena_adapter *adapter, u64 **data)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM_PHC; i++) {
		ena_stats = &ena_stats_ena_com_phc_strings[i];
		ptr = (u64 *)&adapter->ena_dev->phc.stats + ena_stats->stat_offset;
		*(*data)++ = *ptr;
	}
}

static u64 *ena_get_base_stats(struct ena_adapter *adapter,
			       u64 *data,
			       bool hw_stats_needed)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];

		ptr = (u64 *)&adapter->dev_stats + ena_stats->stat_offset;

		ena_safe_update_stat(ptr, data++, &adapter->syncp);
	}

	if (hw_stats_needed)
		ena_metrics_stats(adapter, &data);

	ena_get_admin_queue_stats(adapter, &data);

	if (ena_phc_is_active(adapter))
		ena_get_phc_stats(adapter, &data);

	return data;
}

static void ena_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats,
				  u64 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	data = ena_get_base_stats(adapter, data, true);
	ena_get_queue_stats(adapter, data, false);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
#ifdef ENA_HAVE_KERNEL_ETHTOOL_TS_INFO
static int ena_get_ts_info(struct net_device *netdev,
			   struct kernel_ethtool_ts_info *info)
#else
static int ena_get_ts_info(struct net_device *netdev,
			   struct ethtool_ts_info *info)
#endif /* ENA_HAVE_KERNEL_ETHTOOL_TS_INFO */
{
	struct ena_adapter *adapter = netdev_priv(netdev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;
#else
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0) */

	info->phc_index = ena_phc_get_index(adapter);

	return 0;
}

#endif
static int ena_get_queue_sw_stats_count(struct ena_adapter *adapter,
					bool count_accumulated_stats)
{
	int count = (adapter->num_io_queues + adapter->xdp_num_queues) *
		    (ENA_ACCUM_STATS_ARRAY_TX + ENA_PER_Q_STATS_ARRAY_TX) +
		     adapter->num_io_queues * ENA_STATS_ARRAY_RX;

	if (count_accumulated_stats)
		count += ENA_STATS_ARRAY_RX + ENA_ACCUM_STATS_ARRAY_TX;

	return count;
}

static int ena_get_base_sw_stats_count(struct ena_adapter *adapter)
{
	int count = ENA_STATS_ARRAY_GLOBAL + ENA_STATS_ARRAY_ENA_COM_ADMIN;

	if (ena_phc_is_active(adapter))
		count += ENA_STATS_ARRAY_ENA_COM_PHC;

	return count;
}

static int ena_get_hw_stats_count(struct ena_adapter *adapter)
{
	struct ena_com_dev *dev = adapter->ena_dev;
	int count;

	count = ENA_STATS_ARRAY_ENA_SRD * ena_com_get_cap(dev, ENA_ADMIN_ENA_SRD_INFO);

	if (ena_com_get_cap(dev, ENA_ADMIN_CUSTOMER_METRICS))
		count += ena_com_get_customer_metric_count(dev);
	else if (ena_com_get_cap(dev, ENA_ADMIN_ENI_STATS))
		count += ENA_STATS_ARRAY_ENI;

	return count;
}

int ena_get_sset_count(struct net_device *netdev, int sset)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return ena_get_base_sw_stats_count(adapter) +
		       ena_get_queue_sw_stats_count(adapter, false) +
		       ena_get_hw_stats_count(adapter);
	case ETH_SS_PRIV_FLAGS:
		return ENA_PRIV_FLAGS_NR;
	}

	return -EOPNOTSUPP;
}

static void ena_metrics_stats_strings(struct ena_adapter *adapter, u8 **data)
{
	struct ena_com_dev *dev = adapter->ena_dev;
	const struct ena_hw_metrics *ena_metrics;
	const struct ena_stats *ena_stats;
	int i;

	if (ena_com_get_cap(dev, ENA_ADMIN_CUSTOMER_METRICS)) {
		for (i = 0; i < ENA_METRICS_ARRAY_ENI; i++) {
			if (ena_com_get_customer_metric_support(dev, i)) {
				ena_metrics = &ena_hw_stats_strings[i];
				ethtool_puts(data, ena_metrics->name);
			}
		}
	} else if (ena_com_get_cap(dev, ENA_ADMIN_ENI_STATS)) {
		for (i = 0; i < ENA_STATS_ARRAY_ENI; i++) {
			ena_stats = &ena_stats_eni_strings[i];
			ethtool_puts(data, ena_stats->name);
		}
	}

	if (ena_com_get_cap(dev, ENA_ADMIN_ENA_SRD_INFO)) {
		for (i = 0; i < ENA_STATS_ARRAY_ENA_SRD; i++) {
			ena_stats = &ena_srd_info_strings[i];
			ethtool_puts(data, ena_stats->name);
		}
	}
}

static void ena_get_queue_strings(struct ena_adapter *adapter, u8 *data,
				  bool print_accumulated_queue_stats)
{
	u32 queues_nr = adapter->num_io_queues + adapter->xdp_num_queues;
	const struct ena_stats *ena_stats;
	bool is_xdp;
	int i, j;

	if (print_accumulated_queue_stats) {
		for (i = 0; i < ENA_ACCUM_STATS_ARRAY_TX; i++) {
			ena_stats = &ena_accum_stats_tx_strings[i];

			ethtool_sprintf(&data, "total_tx_%s", ena_stats->name);
		}

		for (i = 0; i < ENA_STATS_ARRAY_RX; i++) {
			ena_stats = &ena_stats_rx_strings[i];

			ethtool_sprintf(&data, "total_rx_%s", ena_stats->name);
		}
	}

	for (i = 0; i < queues_nr; i++) {
		is_xdp = ENA_IS_XDP_INDEX(adapter, i);
		/* Tx stats */
		for (j = 0; j < ENA_PER_Q_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_per_q_stats_tx_strings[j];

			ethtool_sprintf(&data, "queue_%u_%s_%s", i,
					is_xdp ? "xdp_tx" : "tx",
					ena_stats->name);
		}

		for (j = 0; j < ENA_ACCUM_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_accum_stats_tx_strings[j];

			ethtool_sprintf(&data, "queue_%u_%s_%s", i,
					is_xdp ? "xdp_tx" : "tx",
					ena_stats->name);
		}

		/* XDP TX queues don't have a RX queue counterpart */
		if (is_xdp)
			continue;

		/* Rx stats */
		for (j = 0; j < ENA_STATS_ARRAY_RX; j++) {
			ena_stats = &ena_stats_rx_strings[j];

			ethtool_sprintf(&data, "queue_%u_rx_%s", i,
					ena_stats->name);
		}
	}
}

static void ena_get_admin_strings(u8 **data)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM_ADMIN; i++) {
		ena_stats = &ena_stats_ena_com_admin_strings[i];

		ethtool_sprintf(data,
				"ena_admin_q_%s", ena_stats->name);
	}
}

static void ena_get_phc_strings(u8 **data)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM_PHC; i++) {
		ena_stats = &ena_stats_ena_com_phc_strings[i];
		ethtool_puts(data, ena_stats->name);
	}
}

static u8 *ena_get_base_strings(struct ena_adapter *adapter,
				 u8 *data,
				 bool hw_stats_needed)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];
		ethtool_puts(&data, ena_stats->name);
	}

	if (hw_stats_needed)
		ena_metrics_stats_strings(adapter, &data);

	ena_get_admin_strings(&data);

	if (ena_phc_is_active(adapter))
		ena_get_phc_strings(&data);

	return data;
}

static void ena_get_ethtool_strings(struct net_device *netdev,
				    u32 sset,
				    u8 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		data = ena_get_base_strings(adapter, data, true);
		ena_get_queue_strings(adapter, data, false);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, ena_priv_flags_strings, sizeof(ena_priv_flags_strings));
		break;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
static int ena_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *link_ksettings)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	struct ena_admin_get_feature_link_desc *link;
	struct ena_admin_get_feat_resp feat_resp;
	int rc;

	rc = ena_com_get_link_params(ena_dev, &feat_resp);
	if (rc)
		return rc;

	link = &feat_resp.u.link;
	link_ksettings->base.speed = link->speed;

	if (link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK) {
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
	}

	link_ksettings->base.autoneg =
		(link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK) ?
		AUTONEG_ENABLE : AUTONEG_DISABLE;

	link_ksettings->base.duplex = DUPLEX_FULL;

	return 0;
}

#else
static int ena_get_settings(struct net_device *netdev,
			    struct ethtool_cmd *ecmd)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	struct ena_admin_get_feature_link_desc *link;
	struct ena_admin_get_feat_resp feat_resp;
	int rc;

	rc = ena_com_get_link_params(ena_dev, &feat_resp);
	if (rc)
		return rc;

	link = &feat_resp.u.link;

	ethtool_cmd_speed_set(ecmd, link->speed);

	if (link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_DUPLEX_MASK)
		ecmd->duplex = DUPLEX_FULL;
	else
		ecmd->duplex = DUPLEX_HALF;

	if (link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK)
		ecmd->autoneg = AUTONEG_ENABLE;
	else
		ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

#endif
static int ena_get_coalesce(struct net_device *net_dev,
#ifdef ENA_EXTENDED_COALESCE_UAPI_WITH_CQE_SUPPORTED
			    struct ethtool_coalesce *coalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
#else
			    struct ethtool_coalesce *coalesce)
#endif
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;

	if (!ena_com_interrupt_moderation_supported(ena_dev))
		return -EOPNOTSUPP;

	coalesce->tx_coalesce_usecs =
		ena_com_get_nonadaptive_moderation_interval_tx(ena_dev) *
			ena_dev->intr_delay_resolution;

	coalesce->rx_coalesce_usecs =
		ena_com_get_nonadaptive_moderation_interval_rx(ena_dev)
		* ena_dev->intr_delay_resolution;

	coalesce->use_adaptive_rx_coalesce =
		ena_com_get_adaptive_moderation_enabled(ena_dev);

	return 0;
}

static void ena_update_tx_rings_nonadaptive_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_tx(adapter->ena_dev);

	for (i = 0; i < adapter->num_io_queues + adapter->xdp_num_queues; i++) {
		adapter->tx_ring[i].interrupt_interval_changed |=
			(adapter->tx_ring[i].interrupt_interval != val);
		adapter->tx_ring[i].interrupt_interval = val;
	}
}

static void ena_update_rx_rings_nonadaptive_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_rx(adapter->ena_dev);

	for (i = 0; i < adapter->num_io_queues + adapter->xdp_num_queues; i++) {
		adapter->rx_ring[i].interrupt_interval_changed |=
			(adapter->rx_ring[i].interrupt_interval != val);
		adapter->rx_ring[i].interrupt_interval = val;
	}
}

static int ena_set_coalesce(struct net_device *net_dev,
#ifdef ENA_EXTENDED_COALESCE_UAPI_WITH_CQE_SUPPORTED
			    struct ethtool_coalesce *coalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
#else
			    struct ethtool_coalesce *coalesce)
#endif
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int rc;

	if (!ena_com_interrupt_moderation_supported(ena_dev))
		return -EOPNOTSUPP;

	rc = ena_com_update_nonadaptive_moderation_interval_tx(ena_dev,
							       coalesce->tx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_tx_rings_nonadaptive_intr_moderation(adapter);

	rc = ena_com_update_nonadaptive_moderation_interval_rx(ena_dev,
							       coalesce->rx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_rx_rings_nonadaptive_intr_moderation(adapter);

	if (coalesce->use_adaptive_rx_coalesce &&
	    !ena_com_get_adaptive_moderation_enabled(ena_dev))
		ena_com_enable_adaptive_moderation(ena_dev);

	if (!coalesce->use_adaptive_rx_coalesce &&
	    ena_com_get_adaptive_moderation_enabled(ena_dev))
		ena_com_disable_adaptive_moderation(ena_dev);

	return 0;
}

static u32 ena_get_msglevel(struct net_device *netdev)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void ena_set_msglevel(struct net_device *netdev, u32 value)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = value;
}

static void ena_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct ena_adapter *adapter = netdev_priv(dev);
	ssize_t ret = 0;

	ret = strscpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	if (ret < 0)
		netif_dbg(adapter, drv, dev,
			  "module name will be truncated, status = %zd\n", ret);

	ret = strscpy(info->version, DRV_MODULE_GENERATION, sizeof(info->version));
	if (ret < 0)
		netif_dbg(adapter, drv, dev,
			  "module version will be truncated, status = %zd\n", ret);

	ret = strscpy(info->bus_info, pci_name(adapter->pdev),
		      sizeof(info->bus_info));
	if (ret < 0)
		netif_dbg(adapter, drv, dev,
			  "bus info will be truncated, status = %zd\n", ret);

	info->n_priv_flags = ENA_PRIV_FLAGS_NR;
}

static void ena_get_ringparam(struct net_device *netdev,
#ifdef ENA_ETHTOOL_RX_BUFF_SIZE_CHANGE
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
#else
			      struct ethtool_ringparam *ring)
#endif
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	ring->tx_max_pending = adapter->max_tx_ring_size;
	ring->rx_max_pending = adapter->max_rx_ring_size;
#ifdef ENA_LARGE_LLQ_ETHTOOL
	if (likely(adapter->ena_dev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV)) {
		bool large_llq_supported = adapter->large_llq_header_supported;

		kernel_ring->tx_push = true;
		kernel_ring->tx_push_buf_len = adapter->ena_dev->tx_max_header_size;
		if (large_llq_supported)
			kernel_ring->tx_push_buf_max_len = ENA_LLQ_LARGE_HEADER;
		else
			kernel_ring->tx_push_buf_max_len = ENA_LLQ_HEADER;
	} else {
		kernel_ring->tx_push = false;
		kernel_ring->tx_push_buf_max_len = 0;
		kernel_ring->tx_push_buf_len = 0;
	}

#endif /* ENA_LARGE_LLQ_ETHTOOL */
	ring->tx_pending = adapter->tx_ring[0].ring_size;
	ring->rx_pending = adapter->rx_ring[0].ring_size;
}

static int ena_set_ringparam(struct net_device *netdev,
#ifdef ENA_ETHTOOL_RX_BUFF_SIZE_CHANGE
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
#else
			     struct ethtool_ringparam *ring)
#endif
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	u32 new_tx_size, new_rx_size, new_tx_push_buf_len;
	bool changed = false;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	new_tx_size = clamp_val(ring->tx_pending, ENA_MIN_RING_SIZE,
				adapter->max_tx_ring_size);
	new_tx_size = rounddown_pow_of_two(new_tx_size);

	new_rx_size = clamp_val(ring->rx_pending, ENA_MIN_RING_SIZE,
				adapter->max_rx_ring_size);
	new_rx_size = rounddown_pow_of_two(new_rx_size);

	changed |= new_tx_size != adapter->requested_tx_ring_size ||
		   new_rx_size != adapter->requested_rx_ring_size;

	/* This value is ignored if LLQ is not supported */
	new_tx_push_buf_len = adapter->ena_dev->tx_max_header_size;
#ifdef ENA_LARGE_LLQ_ETHTOOL

	if ((adapter->ena_dev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) !=
	    kernel_ring->tx_push) {
		NL_SET_ERR_MSG_MOD(extack, "Push mode state cannot be modified");
		return -EINVAL;
	}

	/* Validate that the push buffer is supported on the underlying device */
	if (kernel_ring->tx_push_buf_len) {
		enum ena_admin_placement_policy_type placement;

		new_tx_push_buf_len = kernel_ring->tx_push_buf_len;

		placement = adapter->ena_dev->tx_mem_queue_type;
		if (unlikely(placement == ENA_ADMIN_PLACEMENT_POLICY_HOST))
			return -EOPNOTSUPP;

		if (new_tx_push_buf_len != ENA_LLQ_HEADER &&
		    new_tx_push_buf_len != ENA_LLQ_LARGE_HEADER) {
			bool large_llq_sup = adapter->large_llq_header_supported;
			char large_llq_size_str[40];

			snprintf(large_llq_size_str, 40, ", %lu", ENA_LLQ_LARGE_HEADER);

			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Supported tx push buff values: [%lu%s]",
					       ENA_LLQ_HEADER,
					       large_llq_sup ? large_llq_size_str : "");

			return -EINVAL;
		}

		changed |= new_tx_push_buf_len != adapter->ena_dev->tx_max_header_size;
	}

#endif
	if (!changed)
		return 0;

	return ena_update_queue_params(adapter, new_tx_size, new_rx_size,
				       new_tx_push_buf_len);
}

#ifdef ETHTOOL_GRXRINGS
static u32 ena_flow_hash_to_flow_type(u16 hash_fields)
{
	u32 data = 0;

	if (hash_fields & ENA_ADMIN_RSS_L2_DA)
		data |= RXH_L2DA;

	if (hash_fields & ENA_ADMIN_RSS_L3_DA)
		data |= RXH_IP_DST;

	if (hash_fields & ENA_ADMIN_RSS_L3_SA)
		data |= RXH_IP_SRC;

	if (hash_fields & ENA_ADMIN_RSS_L4_DP)
		data |= RXH_L4_B_2_3;

	if (hash_fields & ENA_ADMIN_RSS_L4_SP)
		data |= RXH_L4_B_0_1;

	return data;
}

static u16 ena_flow_data_to_flow_hash(u32 hash_fields)
{
	u16 data = 0;

	if (hash_fields & RXH_L2DA)
		data |= ENA_ADMIN_RSS_L2_DA;

	if (hash_fields & RXH_IP_DST)
		data |= ENA_ADMIN_RSS_L3_DA;

	if (hash_fields & RXH_IP_SRC)
		data |= ENA_ADMIN_RSS_L3_SA;

	if (hash_fields & RXH_L4_B_2_3)
		data |= ENA_ADMIN_RSS_L4_DP;

	if (hash_fields & RXH_L4_B_0_1)
		data |= ENA_ADMIN_RSS_L4_SP;

	return data;
}

static int ena_get_rss_hash(struct ena_com_dev *ena_dev,
			    struct ethtool_rxnfc *cmd)
{
	enum ena_admin_flow_hash_proto proto;
	u16 hash_fields;
	int rc;

	cmd->data = 0;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		proto = ENA_ADMIN_RSS_TCP4;
		break;
	case UDP_V4_FLOW:
		proto = ENA_ADMIN_RSS_UDP4;
		break;
	case TCP_V6_FLOW:
		proto = ENA_ADMIN_RSS_TCP6;
		break;
	case UDP_V6_FLOW:
		proto = ENA_ADMIN_RSS_UDP6;
		break;
	case IPV4_FLOW:
		proto = ENA_ADMIN_RSS_IP4;
		break;
	case IPV6_FLOW:
		proto = ENA_ADMIN_RSS_IP6;
		break;
	case ETHER_FLOW:
		proto = ENA_ADMIN_RSS_NOT_IP;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	rc = ena_com_get_hash_ctrl(ena_dev, proto, &hash_fields);
	if (unlikely(rc))
		return rc;

	cmd->data = ena_flow_hash_to_flow_type(hash_fields);

	return 0;
}

static int ena_set_rss_hash(struct ena_com_dev *ena_dev,
			    struct ethtool_rxnfc *cmd)
{
	enum ena_admin_flow_hash_proto proto;
	u16 hash_fields;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		proto = ENA_ADMIN_RSS_TCP4;
		break;
	case UDP_V4_FLOW:
		proto = ENA_ADMIN_RSS_UDP4;
		break;
	case TCP_V6_FLOW:
		proto = ENA_ADMIN_RSS_TCP6;
		break;
	case UDP_V6_FLOW:
		proto = ENA_ADMIN_RSS_UDP6;
		break;
	case IPV4_FLOW:
		proto = ENA_ADMIN_RSS_IP4;
		break;
	case IPV6_FLOW:
		proto = ENA_ADMIN_RSS_IP6;
		break;
	case ETHER_FLOW:
		proto = ENA_ADMIN_RSS_NOT_IP;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	hash_fields = ena_flow_data_to_flow_hash(cmd->data);

	return ena_com_fill_hash_ctrl(ena_dev, proto, hash_fields);
}

static int ena_set_steering_rule(struct ena_com_dev *ena_dev, struct ethtool_rxnfc *info)
{
	struct ena_com_flow_steering_rule_params rule_params = {};
	struct ena_admin_flow_steering_rule_params *flow_params;
	struct ethtool_rx_flow_spec *fs = &info->fs;
	struct ethtool_tcpip4_spec *tcp_ip4;
	struct ethtool_usrip4_spec *usr_ip4;
#ifdef ENA_ETHTOOL_NFC_IPV6_SUPPORTED
	struct ethtool_tcpip6_spec *tcp_ip6;
	struct ethtool_usrip6_spec *usr_ip6;
#endif
	u32 flow_type = info->fs.flow_type;
	u16 rule_idx;
	int rc;

	flow_params = &rule_params.flow_params;

	/* no support for wake-on-lan or packets drop for rule matching */
	if ((fs->ring_cookie == RX_CLS_FLOW_DISC) || (fs->ring_cookie == RX_CLS_FLOW_WAKE))
		return -EOPNOTSUPP;

	/* no support for any special rule placement */
	if (fs->location & RX_CLS_LOC_SPECIAL)
		return -EOPNOTSUPP;

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		if (flow_type == TCP_V4_FLOW)
			rule_params.flow_type = ENA_ADMIN_FLOW_IPV4_TCP;
		else
			rule_params.flow_type = ENA_ADMIN_FLOW_IPV4_UDP;

		tcp_ip4 = &fs->h_u.tcp_ip4_spec;
		memcpy(flow_params->src_ip, &tcp_ip4->ip4src, sizeof(tcp_ip4->ip4src));
		memcpy(flow_params->dst_ip, &tcp_ip4->ip4dst, sizeof(tcp_ip4->ip4dst));
		flow_params->src_port = htons(tcp_ip4->psrc);
		flow_params->dst_port = htons(tcp_ip4->pdst);
		flow_params->tos = tcp_ip4->tos;

		tcp_ip4 = &fs->m_u.tcp_ip4_spec;
		memcpy(flow_params->src_ip_mask, &tcp_ip4->ip4src, sizeof(tcp_ip4->ip4src));
		memcpy(flow_params->dst_ip_mask, &tcp_ip4->ip4dst, sizeof(tcp_ip4->ip4dst));
		flow_params->src_port_mask = htons(tcp_ip4->psrc);
		flow_params->dst_port_mask = htons(tcp_ip4->pdst);
		flow_params->tos_mask = tcp_ip4->tos;
		break;
	case IP_USER_FLOW:
		rule_params.flow_type = ENA_ADMIN_FLOW_IPV4;

		usr_ip4 = &fs->h_u.usr_ip4_spec;
		memcpy(flow_params->src_ip, &usr_ip4->ip4src, sizeof(usr_ip4->ip4src));
		memcpy(flow_params->dst_ip, &usr_ip4->ip4dst, sizeof(usr_ip4->ip4dst));
		flow_params->tos = usr_ip4->tos;

		usr_ip4 = &fs->m_u.usr_ip4_spec;
		memcpy(flow_params->src_ip_mask, &usr_ip4->ip4src, sizeof(usr_ip4->ip4src));
		memcpy(flow_params->dst_ip_mask, &usr_ip4->ip4dst, sizeof(usr_ip4->ip4dst));
		flow_params->tos_mask = usr_ip4->tos;
		break;
#ifdef ENA_ETHTOOL_NFC_IPV6_SUPPORTED
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		if (flow_type == TCP_V6_FLOW)
			rule_params.flow_type = ENA_ADMIN_FLOW_IPV6_TCP;
		else
			rule_params.flow_type = ENA_ADMIN_FLOW_IPV6_UDP;

		tcp_ip6 = &fs->h_u.tcp_ip6_spec;
		memcpy(flow_params->src_ip, &tcp_ip6->ip6src, sizeof(tcp_ip6->ip6src));
		memcpy(flow_params->dst_ip, &tcp_ip6->ip6dst, sizeof(tcp_ip6->ip6dst));
		flow_params->src_port = htons(tcp_ip6->psrc);
		flow_params->dst_port = htons(tcp_ip6->pdst);

		tcp_ip6 = &fs->m_u.tcp_ip6_spec;
		memcpy(flow_params->src_ip_mask, &tcp_ip6->ip6src, sizeof(tcp_ip6->ip6src));
		memcpy(flow_params->dst_ip_mask, &tcp_ip6->ip6dst, sizeof(tcp_ip6->ip6dst));
		flow_params->src_port_mask = htons(tcp_ip6->psrc);
		flow_params->dst_port_mask = htons(tcp_ip6->pdst);
		break;
	case IPV6_USER_FLOW:
		rule_params.flow_type = ENA_ADMIN_FLOW_IPV6;

		usr_ip6 = &fs->h_u.usr_ip6_spec;
		memcpy(flow_params->src_ip, &usr_ip6->ip6src, sizeof(usr_ip6->ip6src));
		memcpy(flow_params->dst_ip, &usr_ip6->ip6dst, sizeof(usr_ip6->ip6dst));

		usr_ip6 = &fs->m_u.usr_ip6_spec;
		memcpy(flow_params->src_ip_mask, &usr_ip6->ip6src, sizeof(usr_ip6->ip6src));
		memcpy(flow_params->dst_ip_mask, &usr_ip6->ip6dst, sizeof(usr_ip6->ip6dst));
		break;
#endif
	case SCTP_V4_FLOW:
	case AH_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V4_FLOW:
	case ESP_V6_FLOW:
	case ETHER_FLOW:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	rule_params.qid = fs->ring_cookie;
	rule_idx = fs->location;

	rc = ena_com_flow_steering_add_rule(ena_dev, &rule_params, &rule_idx);
	if (unlikely(rc < 0))
		return rc;

	return 0;
}

static int ena_delete_steering_rule(struct ena_com_dev *ena_dev, struct ethtool_rxnfc *info)
{
	return ena_com_flow_steering_remove_rule(ena_dev, info->fs.location);
}

static int ena_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *info)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		rc = ena_set_rss_hash(adapter->ena_dev, info);
		break;
	case ETHTOOL_SRXCLSRLINS:
		rc = ena_set_steering_rule(adapter->ena_dev, info);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		rc = ena_delete_steering_rule(adapter->ena_dev, info);
		break;
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter %d is not supported\n", info->cmd);
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int ena_get_steering_rules_cnt(struct ena_com_dev *ena_dev, struct ethtool_rxnfc *info)
{
	if (!(ena_dev->supported_features & BIT(ENA_ADMIN_FLOW_STEERING_CONFIG)))
		return -EOPNOTSUPP;

	info->rule_cnt = ena_dev->flow_steering.active_rules_cnt;
	info->data = ena_dev->flow_steering.tbl_size | RX_CLS_LOC_SPECIAL;

	return 0;
}

static int ena_get_steering_rule(struct ena_com_dev *ena_dev, struct ethtool_rxnfc *info)
{
	struct ena_com_flow_steering_rule_params rule_params = {};
	struct ena_admin_flow_steering_rule_params *flow_params;
	struct ethtool_rx_flow_spec *fs = &info->fs;
	struct ethtool_tcpip4_spec *tcp_ip4;
	struct ethtool_usrip4_spec *usr_ip4;
#ifdef ENA_ETHTOOL_NFC_IPV6_SUPPORTED
	struct ethtool_tcpip6_spec *tcp_ip6;
	struct ethtool_usrip6_spec *usr_ip6;
#endif
	u32 flow_type;
	int rc = 0;

	flow_params = &rule_params.flow_params;

	rc = ena_com_flow_steering_get_rule(ena_dev, &rule_params, fs->location);
	if (unlikely(rc))
		return rc;

	flow_type = rule_params.flow_type;

	switch (flow_type) {
	case ENA_ADMIN_FLOW_IPV4_TCP:
	case ENA_ADMIN_FLOW_IPV4_UDP:
		if (flow_type == ENA_ADMIN_FLOW_IPV4_TCP)
			fs->flow_type = TCP_V4_FLOW;
		else
			fs->flow_type = UDP_V4_FLOW;

		tcp_ip4 = &fs->h_u.tcp_ip4_spec;
		memcpy(&tcp_ip4->ip4src, flow_params->src_ip, sizeof(tcp_ip4->ip4src));
		memcpy(&tcp_ip4->ip4dst, flow_params->dst_ip, sizeof(tcp_ip4->ip4dst));
		tcp_ip4->psrc = ntohs(flow_params->src_port);
		tcp_ip4->pdst = ntohs(flow_params->dst_port);
		tcp_ip4->tos = flow_params->tos;

		tcp_ip4 = &fs->m_u.tcp_ip4_spec;
		memcpy(&tcp_ip4->ip4src, flow_params->src_ip_mask, sizeof(tcp_ip4->ip4src));
		memcpy(&tcp_ip4->ip4dst, flow_params->dst_ip_mask, sizeof(tcp_ip4->ip4dst));
		tcp_ip4->psrc = ntohs(flow_params->src_port_mask);
		tcp_ip4->pdst = ntohs(flow_params->dst_port_mask);
		tcp_ip4->tos = flow_params->tos_mask;
		break;
	case ENA_ADMIN_FLOW_IPV4:
		fs->flow_type = IP_USER_FLOW;

		usr_ip4 = &fs->h_u.usr_ip4_spec;
		memcpy(&usr_ip4->ip4src, flow_params->src_ip, sizeof(usr_ip4->ip4src));
		memcpy(&usr_ip4->ip4dst, flow_params->dst_ip, sizeof(usr_ip4->ip4dst));
		usr_ip4->tos = flow_params->tos;
		usr_ip4->ip_ver = ETH_RX_NFC_IP4;

		usr_ip4 = &fs->m_u.usr_ip4_spec;
		memcpy(&usr_ip4->ip4src, flow_params->src_ip_mask, sizeof(usr_ip4->ip4src));
		memcpy(&usr_ip4->ip4dst, flow_params->dst_ip_mask, sizeof(usr_ip4->ip4dst));
		usr_ip4->tos = flow_params->tos_mask;
		break;
#ifdef ENA_ETHTOOL_NFC_IPV6_SUPPORTED
	case ENA_ADMIN_FLOW_IPV6_TCP:
	case ENA_ADMIN_FLOW_IPV6_UDP:
		if (flow_type == ENA_ADMIN_FLOW_IPV6_TCP)
			fs->flow_type = TCP_V6_FLOW;
		else
			fs->flow_type = UDP_V6_FLOW;

		tcp_ip6 = &fs->h_u.tcp_ip6_spec;
		memcpy(&tcp_ip6->ip6src, flow_params->src_ip, sizeof(tcp_ip6->ip6src));
		memcpy(&tcp_ip6->ip6dst, flow_params->dst_ip, sizeof(tcp_ip6->ip6dst));
		tcp_ip6->psrc = ntohs(flow_params->src_port);
		tcp_ip6->pdst = ntohs(flow_params->dst_port);

		tcp_ip6 = &fs->m_u.tcp_ip6_spec;
		memcpy(&tcp_ip6->ip6src, flow_params->src_ip_mask, sizeof(tcp_ip6->ip6src));
		memcpy(&tcp_ip6->ip6dst, flow_params->dst_ip_mask, sizeof(tcp_ip6->ip6dst));
		tcp_ip6->psrc = ntohs(flow_params->src_port_mask);
		tcp_ip6->pdst = ntohs(flow_params->dst_port_mask);
		break;
	case ENA_ADMIN_FLOW_IPV6:
		fs->flow_type = IPV6_USER_FLOW;

		usr_ip6 = &fs->h_u.usr_ip6_spec;
		memcpy(&usr_ip6->ip6src, flow_params->src_ip, sizeof(usr_ip6->ip6src));
		memcpy(&usr_ip6->ip6dst, flow_params->dst_ip, sizeof(usr_ip6->ip6dst));

		usr_ip6 = &fs->m_u.usr_ip6_spec;
		memcpy(&usr_ip6->ip6src, flow_params->src_ip_mask, sizeof(usr_ip6->ip6src));
		memcpy(&usr_ip6->ip6dst, flow_params->dst_ip_mask, sizeof(usr_ip6->ip6dst));
		break;
#endif
	default:
		netdev_err(ena_dev->net_device,
			   "Flow steering rule received has invalid flow type %u\n",
			   flow_type);
		rc = -EINVAL;
		break;
	}

	fs->ring_cookie = rule_params.qid;

	return rc;
}

static int ena_get_all_steering_rules(struct ena_com_dev *ena_dev, struct ethtool_rxnfc *info,
				      u32 *rules)
{
	struct ena_com_flow_steering *flow_steering = &ena_dev->flow_steering;
	int i, loc_idx = 0;

	if (!(ena_dev->supported_features & BIT(ENA_ADMIN_FLOW_STEERING_CONFIG)))
		return -EOPNOTSUPP;

	info->data = flow_steering->tbl_size;
	for (i = 0; i < flow_steering->tbl_size; i++) {
		if (flow_steering->flow_steering_tbl[i].in_use) {
			/* to avoid access out of bounds index in case
			 * the rules buf provided is too small
			 */
			if (loc_idx >= info->rule_cnt)
				return -EMSGSIZE;

			/* the loop iterator represents the rule location */
			rules[loc_idx++] = i;

		}
	}

	info->rule_cnt = loc_idx;

	return 0;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 2, 0)
static int ena_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *info,
			 void *rules)
#else
static int ena_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *info,
			 u32 *rules)
#endif
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = adapter->num_io_queues;
		rc = 0;
		break;
	case ETHTOOL_GRXFH:
		rc = ena_get_rss_hash(adapter->ena_dev, info);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		rc = ena_get_steering_rules_cnt(adapter->ena_dev, info);
		break;
	case ETHTOOL_GRXCLSRULE:
		rc = ena_get_steering_rule(adapter->ena_dev, info);
		break;
	case ETHTOOL_GRXCLSRLALL:
		rc = ena_get_all_steering_rules(adapter->ena_dev, info, rules);
		break;
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter %d is not supported\n", info->cmd);
		rc = -EOPNOTSUPP;
	}

	return rc;
}
#endif /* ETHTOOL_GRXRINGS */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static u32 ena_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	return get_rss_indirection_table_size(adapter);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static u32 ena_get_rxfh_key_size(struct net_device *netdev)
{
	return ENA_HASH_KEY_SIZE;
}
#endif

static int ena_indirection_table_set(struct ena_adapter *adapter,
				     const u32 *indir)
{
	u32 table_size = get_rss_indirection_table_size(adapter);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int i, rc;

	if (table_size == 0)
		return -EOPNOTSUPP;

	for (i = 0; i < table_size; i++) {
		rc = ena_com_indirect_table_fill_entry(ena_dev,
						       i,
						       ENA_IO_RXQ_IDX(indir[i]));
		if (unlikely(rc)) {
			netif_err(adapter, drv, adapter->netdev,
				  "Cannot fill indirect table (index is too large)\n");
			return rc;
		}
	}

	rc = ena_com_indirect_table_set(ena_dev);
	if (rc) {
		netif_err(adapter, drv, adapter->netdev,
			  "Cannot set indirect table\n");
		return rc == -EPERM ? -EOPNOTSUPP : rc;
	}
	return rc;
}

static int ena_indirection_table_get(struct ena_adapter *adapter, u32 *indir)
{
	u32 table_size = get_rss_indirection_table_size(adapter);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int i, rc;

	if (table_size == 0)
		return -EOPNOTSUPP;

	if (!indir)
		return 0;

	rc = ena_com_indirect_table_get(ena_dev, indir);
	if (unlikely(rc))
		return rc;

	/* Our internal representation of the indices is: even indices
	 * for Tx and uneven indices for Rx. We need to convert the Rx
	 * indices to be consecutive
	 */
	for (i = 0; i < table_size; i++)
		indir[i] = ENA_IO_RXQ_IDX_TO_COMBINED_IDX(indir[i]);

	return rc;
}

#ifdef ENA_HAVE_ETHTOOL_RXFH_PARAM
static int ena_get_rxfh(struct net_device *netdev,
			struct ethtool_rxfh_param *rxfh)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int ena_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
#endif /* ENA_HAVE_ETHTOOL_RXFH_PARAM */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	enum ena_admin_hash_functions ena_func;
#ifdef ENA_HAVE_ETHTOOL_RXFH_PARAM
	u32 *indir = rxfh->indir;
	u8 *hfunc = &rxfh->hfunc;
	u8 *key = rxfh->key;
#endif /* ENA_HAVE_ETHTOOL_RXFH_PARAM */
	u8 func;
	int rc;

	rc = ena_indirection_table_get(adapter, indir);
	if (unlikely(rc))
		return rc;

	/* We call this function in order to check if the device
	 * supports getting/setting the hash function.
	 */
	rc = ena_com_get_hash_function(adapter->ena_dev, &ena_func);
	if (rc) {
		if (rc == -EOPNOTSUPP)
			rc = 0;

		return rc;
	}

	rc = ena_com_get_hash_key(adapter->ena_dev, key);
	if (rc)
		return rc;

	switch (ena_func) {
	case ENA_ADMIN_TOEPLITZ:
		func = ETH_RSS_HASH_TOP;
		break;
	case ENA_ADMIN_CRC32:
		func = ETH_RSS_HASH_CRC32;
		break;
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter is not supported\n");
		return -EOPNOTSUPP;
	}

#ifdef ENA_HAVE_ETHTOOL_RXFH_PARAM
	*hfunc = func;
#else
	if (hfunc)
		*hfunc = func;
#endif /* ENA_HAVE_ETHTOOL_RXFH_PARAM */

	return 0;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int ena_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	enum ena_admin_hash_functions ena_func;
	int rc;

	rc = ena_indirection_table_get(adapter, indir);
	if (unlikely(rc))
		return rc;

	/* We call this function in order to check if the device
	 * supports getting/setting the hash function.
	 */
	rc = ena_com_get_hash_function(adapter->ena_dev, &ena_func);
	if (rc) {
		if (rc == -EOPNOTSUPP)
			rc = 0;

		return rc;
	}

	rc = ena_com_get_hash_key(adapter->ena_dev, key);
	if (rc)
		return rc;

	return rc;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)/* >= 3.16.0 */
static int ena_get_rxfh(struct net_device *netdev, u32 *indir)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	return ena_indirection_table_get(adapter, indir);
}
#endif /* >= 3.8.0 */

#ifdef ENA_HAVE_ETHTOOL_RXFH_PARAM
static int ena_set_rxfh(struct net_device *netdev,
			struct ethtool_rxfh_param *rxfh,
			struct netlink_ext_ack *extack)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int ena_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int ena_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key)
#endif /* ENA_HAVE_ETHTOOL_RXFH_PARAM */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	enum ena_admin_hash_functions func = 0;
#ifdef ENA_HAVE_ETHTOOL_RXFH_PARAM
	u32 *indir = rxfh->indir;
	u8 hfunc = rxfh->hfunc;
	u8 *key = rxfh->key;
#endif /* ENA_HAVE_ETHTOOL_RXFH_PARAM */
	int rc;

	if (indir) {
		rc = ena_indirection_table_set(adapter, indir);
		if (rc)
			return rc;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	switch (hfunc) {
	case ETH_RSS_HASH_NO_CHANGE:
		func = ena_com_get_current_hash_function(ena_dev);
		break;
	case ETH_RSS_HASH_TOP:
		func = ENA_ADMIN_TOEPLITZ;
		break;
	case ETH_RSS_HASH_CRC32:
		func = ENA_ADMIN_CRC32;
		break;
	default:
		netif_err(adapter, drv, netdev, "Unsupported hfunc %d\n",
			  hfunc);
		return -EOPNOTSUPP;
	}
#else /* Kernel 3.19 */
	func = ENA_ADMIN_TOEPLITZ;
#endif

	if (key || func) {
		rc = ena_com_fill_hash_function(ena_dev, func, key,
						ENA_HASH_KEY_SIZE,
						0xFFFFFFFF);
		if (unlikely(rc)) {
			netif_err(adapter, drv, netdev, "Cannot fill key\n");
			return rc == -EPERM ? -EOPNOTSUPP : rc;
		}
	}

	return 0;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) /* Kernel > 3.16 */
static int ena_set_rxfh(struct net_device *netdev, const u32 *indir)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	if (indir)
		rc = ena_indirection_table_set(adapter, indir);

	return rc;
}
#endif /* Kernel >= 3.8 */
#endif /* ETHTOOL_GRXFH */
#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT

#ifdef ETHTOOL_SCHANNELS
static void ena_get_channels(struct net_device *netdev,
			     struct ethtool_channels *channels)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	channels->max_combined = adapter->max_num_io_queues;
	channels->combined_count = adapter->num_io_queues;
}

static int ena_set_channels(struct net_device *netdev,
			    struct ethtool_channels *channels)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	u32 count = channels->combined_count;
	/* The check for max value is already done in ethtool */
	if (count < ENA_MIN_NUM_IO_QUEUES)
		return -EINVAL;

	if (!ena_xdp_legal_queue_count(adapter, count)) {
		if (ena_xdp_present(adapter))
			return -EINVAL;

		xdp_clear_features_flag(netdev);
	} else {
		xdp_set_features_flag(netdev, ENA_XDP_FEATURES);
	}

	if (count > adapter->max_num_io_queues)
		return -EINVAL;

#ifdef ENA_AF_XDP_SUPPORT
	if (count != adapter->num_io_queues && ena_is_zc_q_exist(adapter)) {
		netdev_err(adapter->netdev,
			   "Changing channel count not supported with xsk pool loaded\n");
		return -EOPNOTSUPP;
	}

#endif /* ENA_AF_XDP_SUPPORT */
	return ena_update_queue_count(adapter, count);
}
#endif /* ETHTOOL_SCHANNELS */

#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
static int ena_get_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *tuna, void *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = adapter->rx_copybreak;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ena_set_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *tuna,
			   const void *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int ret = 0;
	u32 len;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		len = *(u32 *)data;
		ret = ena_set_rx_copybreak(adapter, len);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif /* 3.18.0 */

static u32 ena_get_priv_flags(struct net_device *netdev)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	u32 priv_flags = 0;

	if (adapter->rx_ring->page_cache)
		priv_flags |= ENA_PRIV_FLAGS_LPC;

	return priv_flags;
}

static int ena_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	/* LPC is the only supported private flag for now */
	return ena_set_lpc_state(adapter, !!(priv_flags & ENA_PRIV_FLAGS_LPC));
}

static const struct ethtool_ops ena_ethtool_ops = {
#ifdef ENA_HAVE_ETHTOOL_OPS_SUPPORTED_COALESCE_PARAMS
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
#endif
#ifdef ENA_LARGE_LLQ_ETHTOOL
	.supported_ring_params	= ETHTOOL_RING_USE_TX_PUSH_BUF_LEN |
				  ETHTOOL_RING_USE_TX_PUSH,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	.get_link_ksettings	= ena_get_link_ksettings,
#else
	.get_settings		= ena_get_settings,
#endif
	.get_drvinfo		= ena_get_drvinfo,
	.get_msglevel		= ena_get_msglevel,
	.set_msglevel		= ena_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= ena_get_coalesce,
	.set_coalesce		= ena_set_coalesce,
	.get_ringparam		= ena_get_ringparam,
	.set_ringparam		= ena_set_ringparam,
	.get_sset_count         = ena_get_sset_count,
	.get_strings		= ena_get_ethtool_strings,
	.get_ethtool_stats      = ena_get_ethtool_stats,
#ifdef ETHTOOL_GRXRINGS
	.get_rxnfc		= ena_get_rxnfc,
	.set_rxnfc		= ena_set_rxnfc,
#endif /* ETHTOOL_GRXRINGS */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	.get_rxfh_indir_size    = ena_get_rxfh_indir_size,
#endif /* >= 3.8.0 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	.get_rxfh_key_size	= ena_get_rxfh_key_size,
	.get_rxfh		= ena_get_rxfh,
	.set_rxfh		= ena_set_rxfh,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	.get_rxfh_indir		= ena_get_rxfh,
	.set_rxfh_indir		= ena_set_rxfh,
#endif /* >= 3.8.0 */
#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
#ifdef ETHTOOL_SCHANNELS
	.get_channels		= ena_get_channels,
	.set_channels		= ena_set_channels,
#endif /* ETHTOOL_SCHANNELS */
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	.get_tunable		= ena_get_tunable,
	.set_tunable		= ena_set_tunable,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	.get_ts_info		= ena_get_ts_info,
#endif
	.get_priv_flags		= ena_get_priv_flags,
	.set_priv_flags		= ena_set_priv_flags,
};

void ena_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ena_ethtool_ops;
}

static void ena_dump_stats_ex(struct ena_adapter *adapter, u8 *buf)
{
	u32 queues_nr = adapter->num_io_queues + adapter->xdp_num_queues;
	u8 *base_strings_buf = NULL, *queue_strings_buf = NULL;
	int base_strings_num, queue_strings_num, i, j, k, rc;
	u64 *base_data_buf = NULL, *queue_data_buf = NULL;
	struct net_device *netdev = adapter->netdev;
	bool print_accumulated_stats = false;

	if (!buf)
		print_accumulated_stats = true;

	base_strings_num = ena_get_base_sw_stats_count(adapter);
	queue_strings_num = ena_get_queue_sw_stats_count(adapter,
							 print_accumulated_stats);

	base_strings_buf = kcalloc(base_strings_num, ETH_GSTRING_LEN, GFP_ATOMIC);
	if (!base_strings_buf) {
		netif_err(adapter, drv, netdev,
			"Failed to allocate base_strings_buf\n");
		return;
	}

	queue_strings_buf = kcalloc(queue_strings_num, ETH_GSTRING_LEN, GFP_ATOMIC);
	if (!queue_strings_buf) {
		netif_err(adapter, drv, netdev,
			"Failed to allocate queue_strings_buf\n");
		goto free_resources;
	}

	base_data_buf = kcalloc(base_strings_num, sizeof(u64), GFP_ATOMIC);
	if (!base_data_buf) {
		netif_err(adapter, drv, netdev,
			"Failed to allocate base_data_buf\n");
		goto free_resources;
	}

	queue_data_buf = kcalloc(queue_strings_num, sizeof(u64), GFP_ATOMIC);
	if (!queue_data_buf) {
		netif_err(adapter, drv, netdev,
			"Failed to allocate queue_data_buf\n");
		goto free_resources;
	}

	ena_get_base_strings(adapter, base_strings_buf, false);
	ena_get_queue_strings(adapter, queue_strings_buf, print_accumulated_stats);

	ena_get_base_stats(adapter, base_data_buf, false);
	ena_get_queue_stats(adapter, queue_data_buf, print_accumulated_stats);

	/* If there is a buffer, dump stats, otherwise print them to dmesg */
	if (buf) {
		for (i = 0; i < base_strings_num; i++) {
			rc = snprintf(buf, ETH_GSTRING_LEN + sizeof(u64),
				      "%s %llu\n",
				      base_strings_buf + i * ETH_GSTRING_LEN,
				      base_data_buf[i]);
			buf += rc;
		}

		for (i = 0; i < queue_strings_num; i++) {
			rc = snprintf(buf, ETH_GSTRING_LEN + sizeof(u64),
				      "%s %llu\n",
				      queue_strings_buf + i * ETH_GSTRING_LEN,
				      queue_data_buf[i]);
			buf += rc;
		}
	} else {
		for (i = 0; i < base_strings_num; i++)
			netif_err(adapter, drv, netdev, "%s: %llu\n",
				  base_strings_buf + i * ETH_GSTRING_LEN,
				  base_data_buf[i]);

		/* Print accumulated queue stats */
		for (i = 0; i < ENA_ACCUM_STATS_ARRAY_TX + ENA_STATS_ARRAY_RX; i++)
			netif_err(adapter, drv, netdev, "%s: %llu\n",
				  queue_strings_buf + i * ETH_GSTRING_LEN,
				  queue_data_buf[i]);

		/* Print per queue stats */
		for (j = 0; j < queues_nr; j++) {
			for (k = 0; k < ENA_PER_Q_STATS_ARRAY_TX; k++, i++)
				netif_err(adapter, drv, netdev, "%s: %llu\n",
					  queue_strings_buf + i * ETH_GSTRING_LEN,
					  queue_data_buf[i]);

			for (k = 0; k < ENA_ACCUM_STATS_ARRAY_TX; k++, i++)
				netif_dbg(adapter, drv, netdev, "%s: %llu\n",
					  queue_strings_buf + i * ETH_GSTRING_LEN,
					  queue_data_buf[i]);

			/* XDP TX queues don't have a RX queue counterpart */
			if (ENA_IS_XDP_INDEX(adapter, j))
				continue;

			for (k = 0; k < ENA_STATS_ARRAY_RX; k++, i++)
				netif_dbg(adapter, drv, netdev, "%s: %llu\n",
					  queue_strings_buf + i * ETH_GSTRING_LEN,
					  queue_data_buf[i]);
		}
	}

free_resources:
	kfree(base_strings_buf);
	kfree(queue_strings_buf);
	kfree(base_data_buf);
	kfree(queue_data_buf);
}

void ena_dump_stats_to_buf(struct ena_adapter *adapter, u8 *buf)
{
	if (!buf)
		return;

	ena_dump_stats_ex(adapter, buf);
}

void ena_dump_stats_to_dmesg(struct ena_adapter *adapter)
{
	ena_dump_stats_ex(adapter, NULL);
}
