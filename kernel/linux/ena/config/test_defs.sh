# This file defines the test snippets used to check Linux kernel features. It
# creates the dummy C files which check if an API is available.
#
# Each feature can be tested using the 'try_compile_async arg1 arg2 arg3 arg4'
# function, where:
#
# * arg1 - is added before the 'int main(void)' definition of the C program
# * arg2 - is added to the body of the 'int main(void)' definition
# * arg3 - will be added to the 'config.h' file if the compilation succeeds
# * arg4 - the macro which the current check depends on
# * arg5 - a logical expression of the form
#
#           kernel_version > A.B.C or A.B.C < kernel_version (all <, >, <=,
#           >= can be used)
#
#          for which arg3 is defined
#
# The 'config.h' is used by the ENA driver to determine which API should be
# used.
#

try_compile_async "#include <linux/pci.h>"               \
                  "pci_dev_id(NULL);"                    \
                  "ENA_HAVE_PCI_DEV_ID"                  \
                  ""                                     \
                  "5.2.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/filter.h>"            \
                  "xdp_do_flush();"                      \
                  "ENA_HAVE_XDP_DO_FLUSH"                \
                  ""                                     \
                  "5.6.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/cpumask.h>"           \
                  "cpumask_local_spread(0, 0);"          \
                  "ENA_HAVE_CPUMASK_LOCAL_SPREAD"        \
                  ""                                     \
                  "4.1.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/interrupt.h>"         \
                  "irq_update_affinity_hint(0, NULL);"   \
                  "ENA_HAVE_UPDATE_AFFINITY_HINT"        \
                  ""                                     \
                  "5.17.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ethtool.h>"           \
                  "ethtool_puts(NULL, NULL);"            \
                  "ENA_HAVE_ETHTOOL_PUTS"                \
                  ""                                     \
                  "6.8.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ethtool.h>"           \
                  "struct ethtool_rxfh_param rxfh;"      \
                  "ENA_HAVE_ETHTOOL_RXFH_PARAM"          \
                  ""                                     \
                  "6.8.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ethtool.h>"                      \
                  "{
                    struct ethtool_ops ops;
                    ops.supported_coalesce_params = 0;
                  }"                                                \
                  "ENA_HAVE_ETHTOOL_OPS_SUPPORTED_COALESCE_PARAMS"  \
                  ""                                                \
                  "5.7.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/etherdevice.h>"       \
                  "eth_hw_addr_set(NULL, NULL);"         \
                  "ENA_HAVE_ETH_HW_ADDR_SET"             \
                  ""                                     \
                  "5.15.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <net/xdp_sock_drv.h>
                  #if ENA_REQUIRE_MIN_VERSION(5, 10, 0)
                  #error
                  #endif"                                \
                  "xsk_buff_dma_sync_for_cpu(NULL);"     \
                  "ENA_XSK_BUFF_DMA_SYNC_SINGLE_ARG"     \
                  ""                                     \
                  "6.10.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/skbuff.h>"            \
                  "__napi_alloc_skb(NULL, 0, 0);"        \
                  "ENA_NAPI_ALLOC_SKB_EXPLICIT_GFP_MASK" \
                  ""                                     \
                  "6.10.0 > LINUX_VERSION_CODE"

try_compile_async "#include <linux/ethtool.h> "               \
                  "struct ethtool_tcpip6_spec tcp_ip6;"       \
                  "ENA_ETHTOOL_NFC_IPV6_SUPPORTED"            \
                  ""                                          \
                  "4.6.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ethtool.h>"           \
                  "{
                    struct kernel_ethtool_ts_info ts;
                    ts.so_timestamping = 0;
                    ts.phc_index = 0;
                  }"                                     \
                  "ENA_HAVE_KERNEL_ETHTOOL_TS_INFO"      \
                  ""                                     \
                  "6.11.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <net/xdp.h>"                                   \
                  "xdp_features_set_redirect_target(NULL, false);"         \
                  "ENA_HAVE_XDP_FEATURES_SET_REDIRECT_TARGET"              \
                  ""                                                       \
                  "6.3.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <net/xdp.h>"                 \
                  "xdp_set_features_flag(NULL, 0);"      \
                  "ENA_HAVE_XDP_SET_FEATURES_FLAG"       \
                  ""                                     \
                  "6.3.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"         \
                  "{
                    struct net_device dev;
                    dev.xdp_features = 0;
                  }"                                     \
                  "ENA_HAVE_NETDEV_XDP_FEATURES"         \
                  ""                                     \
                  "6.3.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ptp_clock_kernel.h>"  \
                  "{
                    struct ptp_clock_info ptp_clk_info;
                    ptp_clk_info.adjfine = NULL;
                  }"                                     \
                  "ENA_PHC_SUPPORT_ADJFINE"              \
                  ""                                     \
                  "4.10.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"         \
                  "{
                    netif_queue_set_napi(NULL, 0, 0, NULL);
                    netif_napi_set_irq(NULL, 0);
                  }"                                     \
                  "ENA_NAPI_IRQ_AND_QUEUE_ASSOC"         \
                  ""                                     \
                  "6.8.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/ptp_clock_kernel.h>"  \
                  "{
                    struct ptp_clock_info ptp_clk_info;
                    ptp_clk_info.adjfreq = NULL;
                  }"                                     \
                  "ENA_PHC_SUPPORT_ADJFREQ"              \
                  ""                                     \
                  "3.0.0 <= LINUX_VERSION_CODE && LINUX_VERSION_CODE < 6.2.0"

try_compile_async "#include <linux/dim.h>"               \
                  "{
                    struct dim_sample sample = {};
                    net_dim(NULL, &sample);
                  }"                                     \
                  "ENA_NET_DIM_SAMPLE_PARAM_BY_REF"      \
                  ""                                     \
                  "6.13.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/skbuff.h>"                 \
                  "skb_frag_fill_page_desc(NULL,NULL,0,0);"   \
                  "ENA_HAVE_SKB_FRAG_FILL_PAGE_DESC"          \
                  ""                                          \
                  "6.5 <= LINUX_VERSION_CODE"

try_compile_async "#include <net/xdp.h>
                   #include <linux/mm.h>
                   #include <linux/bpf.h>
                   #include <linux/skbuff.h>"                             \
                  "{
                     struct bpf_prog_aux aux;
                     struct skb_shared_info shinfo;

                     aux.xdp_has_frags = false;
                     shinfo.xdp_frags_size = 0;

                     xdp_get_buff_len(NULL);
                     xdp_buff_set_frag_pfmemalloc(NULL);
                     xdp_buff_clear_frags_flag(NULL);
                     page_is_pfmemalloc(NULL);
                     xdp_get_shared_info_from_frame(NULL);
                     xdp_buff_has_frags(NULL);
                     xdp_frame_has_frags(NULL);
                     xdp_update_skb_shared_info(NULL, 0, 0, 0, false);
                     xdp_buff_set_frags_flag(NULL);
                     __xdp_rxq_info_reg(NULL, NULL, 0, 0, 0);
                   }"                                                     \
                  "ENA_HAVE_XDP_MB_DEPS"                                  \
                  ""                                                      \
                  "5.18 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"         \
                  "{
                    netif_enable_cpu_rmap(NULL, 0);
                  }"                                     \
                  "ENA_NETIF_ENABLE_CPU_RMAP"            \
                  ""                                     \
                  "6.15.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/skbuff.h>"		 \
                  "skb_metadata_set(NULL,0);"		 \
                  "ENA_HAVE_SKB_METADATA_SET"		 \
                  ""					 \
                  "4.15 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/timer.h>"    \
                  "del_timer(NULL);"            \
                  "ENA_HAVE_DEL_TIMER"          \
                  ""                            \
                  "6.15 > LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"                 \
                  "{
                    netif_napi_add_config(NULL, NULL, NULL, 0);
                  }"                                             \
                  "ENA_NETIF_NAPI_ADD_CONFIG"                    \
                  ""                                             \
                  "6.13.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/atomic.h>"             \
                  "{
                    atomic_t atomic_flag;
                    atomic_set_release(&atomic_flag, 0);
                  }"                                      \
                  "ENA_HAVE_ATOMIC_SET_RELEASE"           \
                  ""                                      \
                  "4.3.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/smp.h>"                      \
                  "{
                    int flag;
                    smp_store_release(&flag, 0);
                   }"                                           \
                  "ENA_HAVE_SMP_STORE_RELEASE"                  \
                  ""                                            \
                  "3.14.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"         \
                  "{
                    struct net_device_ops dev_ops;
                    dev_ops.ndo_eth_ioctl = NULL;
                  }"                                     \
                  "ENA_HAVE_NDO_ETH_IOCTL"               \
                  ""                                     \
                  "5.15.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/netdevice.h>"         \
                  "{
                    struct net_device_ops dev_ops;
                    dev_ops.ndo_hwtstamp_get = NULL;
                    dev_ops.ndo_hwtstamp_set = NULL;
                  }"                                     \
                  "ENA_HAVE_NDO_HWTSTAMP"                \
                  ""                                     \
                  "6.6.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/net_tstamp.h>"            \
                  "{
                    int rx_filter = HWTSTAMP_FILTER_NTP_ALL;
                  }"                                         \
                  "ENA_HAVE_HWTSTAMP_FILTER_NTP_ALL"         \
                  ""                                         \
                  "4.13.0 <= LINUX_VERSION_CODE"
