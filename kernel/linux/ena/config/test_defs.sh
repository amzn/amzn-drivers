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

try_compile_async "#include <net/xdp_sock_drv.h>"        \
                  "xsk_buff_dma_sync_for_cpu(NULL);"     \
                  "ENA_XSK_BUFF_DMA_SYNC_SINGLE_ARG"     \
                  ""                                     \
                  "6.10.0 <= LINUX_VERSION_CODE"

try_compile_async "#include <linux/skbuff.h>"            \
                  "__napi_alloc_skb(NULL, 0, 0);"        \
                  "ENA_NAPI_ALLOC_SKB_EXPLICIT_GFP_MASK" \
                  ""                                     \
                  "6.10.0 > LINUX_VERSION_CODE"

try_compile_async "#include <net/xdp.h>"                           \
                  "NETDEV_XDP_ACT_XSK_ZEROCOPY;"                   \
                  "ENA_HAVE_NETDEV_XDP_ACT_XSK_ZEROCOPY"           \
                  ""                                               \
                  "6.3 <= LINUX_VERSION_CODE"

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
