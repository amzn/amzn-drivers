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
