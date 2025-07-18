# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2021-2025 Amazon.com, Inc. or its affiliates. All rights reserved.

function(config_define def)
  file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/config.h "#define ${def} 1\n")
endfunction()

function(set_conf_tmp_dir prologue body)
  string(RANDOM rand)
  set(tmp_dir "tmp_${rand}")
  set(tmp_dir ${tmp_dir} PARENT_SCOPE)
  configure_file(${CMAKE_SOURCE_DIR}/config/main.c.in ${tmp_dir}/main.c @ONLY)
  configure_file(${CMAKE_SOURCE_DIR}/config/Makefile ${tmp_dir} COPYONLY)
endfunction()

function(try_compile_prog_test)
  set_conf_tmp_dir("" "")
  execute_process(COMMAND make -C ${tmp_dir} KERNEL_DIR=${KERNEL_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE res)
  if(res)
    message(FATAL_ERROR "Conftest failure, kernel headers missing?")
  endif()
  file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/${tmp_dir})
endfunction()

function(wait_for_pid pid)
  execute_process(COMMAND ${CMAKE_SOURCE_DIR}/config/wait_for_pid.sh ${pid})
endfunction()

function(wait_for_pids)
  # Wait for everyone to finish
  foreach(pid ${pids})
    wait_for_pid(${pid})
  endforeach()
endfunction()

function(process_rl)
  list(LENGTH pids list_len)
  if(list_len EQUAL max_process)
    list(GET pids 0 pid)
    wait_for_pid(${pid})
    list(REMOVE_AT pids 0)
    set(pids "${pids}" CACHE INTERNAL "")
  endif()
endfunction()

# CMake "global" variable
set(pids "" CACHE INTERNAL "")

include(ProcessorCount)
ProcessorCount(max_process)

function(try_compile prologue body success_def fail_def)
  # Rate limit processes
  process_rl()
  set_conf_tmp_dir("${prologue}" "${body}")
  execute_process(COMMAND ${CMAKE_SOURCE_DIR}/config/runbg.sh ${CMAKE_SOURCE_DIR}/config/compile_conftest.sh ${CMAKE_CURRENT_BINARY_DIR}/${tmp_dir} ${KERNEL_DIR} "${success_def}" "${fail_def}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE pid)
  list(APPEND pids ${pid})
  set(pids "${pids}" CACHE INTERNAL "")
endfunction()

function(try_compile_dev_or_ops fp_name prologue fn success_def fail_def)
  string(REGEX REPLACE ".*[ \t\r\n]+\\**([A-Za-z0-9_]+)[ \t\r\n]*\\(.*" "\\1" fn_name ${fn})
  try_compile(
  "
${prologue}
static ${fn}
  "
  "
struct ib_device_ops ops = {
  .${fp_name} = ${fn_name},
};
  "
  "${success_def}" "${fail_def}")

  try_compile(
  "
${prologue}
static ${fn}
  "
  "
struct ib_device ibdev = {
  .${fp_name} = ${fn_name},
};
  "
  "${success_def}" "${fail_def}")
endfunction()

file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/config.h)
file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/../output.log)

message("-- Inspecting kernel")
try_compile_prog_test()

try_compile("#include <rdma/ib_umem.h>" "struct ib_umem_chunk chunk;" "" "HAVE_UMEM_SCATTERLIST_IF")

try_compile("" "struct ib_cq_init_attr attr;" HAVE_CREATE_CQ_ATTR "")

try_compile("" "struct rdma_ah_attr attr;" HAVE_CREATE_AH_RDMA_ATTR "")

try_compile_dev_or_ops(post_send ""
  "
int efa_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr, const struct ib_send_wr **bad_wr)
  { return 0; }
  "
  HAVE_POST_CONST_WR "")

try_compile(""
  "
struct ib_device_attr attr = {
  .max_send_sge = 0,
  .max_recv_sge = 0,
};
  "
  HAVE_MAX_SEND_RCV_SGE "")

try_compile("static int port_callback(struct ib_device *ibdev, u8 n, struct kobject *kobj) { return 0; }"
  "
char name[2];
ib_register_device(NULL, name, port_callback);
  "
  HAVE_IB_REGISTER_DEVICE_NAME_PARAM "")

try_compile("" "ib_modify_qp_is_ok(0, 0, 0, 0);" HAVE_IB_MODIFY_QP_IS_OK_FOUR_PARAMS "")

try_compile(""
  "
pgprot_t prot;
rdma_user_mmap_io(NULL, NULL, 0, 0, prot);
  "
  HAVE_RDMA_USER_MMAP_IO "")

try_compile("" "struct ib_device_ops ops;" HAVE_IB_DEV_OPS "")

try_compile_dev_or_ops(create_ah ""
  "struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata) { return NULL; }"
  HAVE_CREATE_DESTROY_AH_FLAGS "")

try_compile(
  "
#include <linux/scatterlist.h>
#include <rdma/ib_umem.h>
  "
  "
int i;
struct ib_umem *umem;
struct sg_dma_page_iter *sg_iter;
for_each_sg_dma_page(umem->sg_head.sgl, sg_iter, umem->nmap, 0)
	i++;
  "
  HAVE_SG_DMA_PAGE_ITER "")

try_compile(""
  "
struct ib_device_ops ops = {
  .size_ib_pd = 0,
};
  "
  HAVE_PD_CORE_ALLOCATION "")

try_compile(""
  "
struct ib_device_ops ops = {
  .size_ib_ucontext = 0,
};
  "
  HAVE_UCONTEXT_CORE_ALLOCATION "")

try_compile(""
  "
struct ib_device dev = {
  .kverbs_provider = 0,
};
  "
  HAVE_NO_KVERBS_DRIVERS "")

try_compile("#include <rdma/ib_umem.h>"
  "
struct ib_udata *udata;
ib_umem_get(udata, 0, 0, 0, 0);
  "
  HAVE_IB_UMEM_GET_UDATA "")

try_compile(
  "
#include <rdma/uverbs_ioctl.h>

struct efa_ucontext {
	struct ib_ucontext ibucontext;
};
  "
  "
struct ib_udata *udata;
rdma_udata_to_drv_context(udata, struct efa_ucontext, ibucontext);
  "
  HAVE_UDATA_TO_DRV_CONTEXT "")

try_compile(""
  "
struct ib_device *dev;
char name[2];
ib_register_device(dev, name);
  "
  HAVE_IB_REGISTER_DEVICE_TWO_PARAMS "")

try_compile(
  "
struct efa_dev {
  struct ib_device ibdev;
};
  "
  "ib_alloc_device(efa_dev, ibdev);" HAVE_SAFE_IB_ALLOC_DEVICE "")

try_compile(""
  "
struct ib_device_ops ops = {
  .size_ib_ah = 0,
};
  "
  HAVE_AH_CORE_ALLOCATION "")

try_compile_dev_or_ops(alloc_pd ""
  "int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) { return 0; }"
  HAVE_ALLOC_PD_NO_UCONTEXT "")

try_compile_dev_or_ops(create_cq ""
  "struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev, const struct ib_cq_init_attr *attr, struct ib_udata *udata) { return NULL; }"
  HAVE_CREATE_CQ_NO_UCONTEXT "")

try_compile_dev_or_ops(dealloc_pd ""
  "void efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) {}"
  HAVE_DEALLOC_PD_UDATA "")

try_compile_dev_or_ops(dereg_mr ""
  "int efa_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata) { return 0; }"
  HAVE_DEREG_MR_UDATA "")

try_compile_dev_or_ops(destroy_qp ""
  "int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata) { return 0; }"
  HAVE_DESTROY_QP_UDATA "")

# This check includes: struct ib_block_iter, __rdma_block_iter_start, __rdma_block_iter_next,
# rdma_block_iter_dma_address, rdma_for_each_block, ib_umem_find_best_pgsz
try_compile("#include <rdma/ib_umem.h>" "ib_umem_find_best_pgsz(NULL, 0, 0);"
  HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE "")

try_compile("" "enum rdma_driver_id driver_id = RDMA_DRIVER_EFA;"
  HAVE_UPSTREAM_EFA "")

try_compile(""
  "
struct ib_device_ops ops = {
  .driver_id = 0,
};
  "
  HAVE_IB_DEVICE_OPS_COMMON "")

try_compile_dev_or_ops(destroy_cq ""
  "void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) {}"
  HAVE_IB_VOID_DESTROY_CQ_UDATA "")

try_compile(""
  "
struct ib_device_ops ops = {
  .size_ib_cq = 0,
};
  "
  HAVE_CQ_CORE_ALLOCATION "")

try_compile(""
  "
struct ib_device dev = {
  .driver_def = 0,
};
  "
  HAVE_IB_DEVICE_DRIVER_DEF "")

try_compile(
  "
#include <rdma/uverbs_ioctl.h>"
  "
struct uverbs_attr_bundle bundle = {
  .driver_udata = {0},
};
  "
  HAVE_ATTR_BUNDLE_DRIVER_UDATA "")

try_compile(""
  "int a = IB_PORT_PHYS_STATE_LINK_UP;"
  HAVE_IB_PORT_PHYS_STATE_LINK_UP "")

try_compile(""
  "
struct ib_device *dev;
ibdev_err_ratelimited(dev, \"Hello\");
  "
  HAVE_IBDEV_PRINT_RATELIMITED "")

try_compile(""
  "
struct ib_device *dev;
ibdev_err(dev, \"World\");
  "
  HAVE_IBDEV_PRINT "")

try_compile("" "int a = IB_QPT_DRIVER;" HAVE_IB_QPT_DRIVER "")

try_compile(""
  "
struct ib_device dev = {
  .driver_id = 0,
};
  "
  HAVE_DRIVER_ID "")

try_compile(""
  "
struct ib_mr mr = {
  .type = 0,
};
  "
  HAVE_IB_MR_TYPE "")

try_compile("#include <linux/pci_ids.h>" "int a = PCI_VENDOR_ID_AMAZON;"
  HAVE_PCI_VENDOR_ID_AMAZON "")

try_compile("#include <rdma/ib_umem.h>" "ib_umem_get(NULL, 0, 0, 0);"
  HAVE_IB_UMEM_GET_NO_DMASYNC "")

try_compile("#include <rdma/ib_umem.h>"
  "
pgprot_t pgt;
rdma_user_mmap_io(NULL, NULL, 0, 0, pgt, NULL);
  "
  HAVE_CORE_MMAP_XA "")

try_compile("" "int a = RDMA_NODE_UNSPECIFIED;" HAVE_RDMA_NODE_UNSPECIFIED "")

try_compile("#include <rdma/ib_umem.h>"
  "
struct ib_device *dev;
ib_umem_get(dev, 0, 0, 0);
  "
  HAVE_IB_UMEM_GET_DEVICE_PARAM "")

try_compile("" "int a = IB_ACCESS_OPTIONAL;" HAVE_IB_ACCESS_OPTIONAL "")

try_compile("" "struct rdma_ah_init_attr ah_attr;" HAVE_CREATE_AH_INIT_ATTR "")

try_compile_dev_or_ops(alloc_mr ""
  "struct ib_mr *efa_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type, u32 max_num_sg, struct ib_udata *udata) { return NULL; }"
  HAVE_ALLOC_MR_UDATA "")

try_compile_dev_or_ops(dealloc_pd ""
  "int efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) { return 0; }"
  HAVE_DEALLOC_PD_UDATA_RC "")

try_compile_dev_or_ops(destroy_ah ""
  "int efa_destroy_ah(struct ib_ah *ibah, u32 flags) { return 0; }"
  HAVE_AH_CORE_ALLOCATION_DESTROY_RC "")

try_compile_dev_or_ops(destroy_cq ""
  "int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) { return 0; }"
  HAVE_IB_INT_DESTROY_CQ_UDATA "")

try_compile("#include <rdma/ib_umem.h>"
  "
struct ib_block_iter biter;
struct ib_umem *umem;
int i;

rdma_umem_for_each_dma_block(umem, &biter, 0)
  i++;
  "
  HAVE_RDMA_UMEM_FOR_EACH_DMA_BLOCK "")

try_compile("#include <rdma/ib_umem.h>"
  "
struct ib_umem *umem;

ib_umem_num_dma_blocks(umem, 0);
  "
  HAVE_IB_UMEM_NUM_DMA_BLOCKS "")

try_compile(""
  "
struct ib_device *dev;
char name[2];
struct device *dma_device;
ib_register_device(dev, name, dma_device);
  "
  HAVE_IB_REGISTER_DEVICE_DMA_DEVICE_PARAM "")

try_compile_dev_or_ops(create_user_ah ""
  "int efa_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr, struct ib_udata *udata) { return 0; }"
  HAVE_UVERBS_CMD_MASK_NOT_NEEDED "")

try_compile_dev_or_ops(query_port ""
  "int efa_query_port(struct ib_device *ibdev, u32 port, struct ib_port_attr *props) { return 0; }"
  HAVE_U32_PORT "")

try_compile_dev_or_ops(alloc_hw_port_stats ""
  "struct rdma_hw_stats *efa_alloc_hw_port_stats(struct ib_device *ibdev, u32 port_num) { return 0; }"
  HAVE_SPLIT_STATS_ALLOC "")

try_compile("#include <linux/sysfs.h>"
  "
sysfs_emit(NULL, \"Test\");
  "
  HAVE_SYSFS_EMIT "")

try_compile("#include <linux/xarray.h>" "xa_load(NULL, 0);" HAVE_XARRAY "")

try_compile(""
  "
struct ib_device_ops ops = {
  .size_ib_qp = 0,
};
  "
  HAVE_QP_CORE_ALLOCATION "")

try_compile("" "struct rdma_stat_desc desc;" HAVE_STAT_DESC_STRUCT "")

# dynamic DMA-buf handling and ibcore device method are required.
try_compile(
  "
#include <linux/dma-resv.h>
#include <linux/dma-buf.h>
  "
  "
static struct dma_buf_attachment a = { .importer_priv = NULL };
struct ib_device_ops ops = {
  .reg_user_mr_dmabuf = NULL,
};
  "
  HAVE_MR_DMABUF "")

try_compile_dev_or_ops(reg_user_mr_dmabuf ""
  "
struct ib_mr *efa_reg_user_mr_dmabuf(struct ib_pd *ibpd, u64 start, u64 length, u64 virt_addr,
                                     int fd, int access_flags, struct uverbs_attr_bundle *attrs) { return 0; }
  "
  HAVE_REG_USER_MR_DMABUF_BUNDLE "")

try_compile("#include <rdma/ib_umem.h>" "ib_umem_dmabuf_get_pinned(NULL, 0, 0, 0, 0);" HAVE_IB_UMEM_DMABUF_PINNED "")

try_compile("#include <linux/module.h>" "MODULE_IMPORT_NS(TEST);" HAVE_MODULE_IMPORT_NS "")

try_compile_dev_or_ops(create_cq ""
  "int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr, struct uverbs_attr_bundle *attrs) { return 0; }"
  HAVE_CREATE_CQ_BUNDLE "")

try_compile_dev_or_ops(create_cq_umem ""
"int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr, struct ib_umem *umem, struct uverbs_attr_bundle *attrs) { return 0; }"
  HAVE_CREATE_CQ_UMEM "")

try_compile("" "fallthrough;" HAVE_FALLTHROUGH "")

try_compile("#include <linux/log2.h>" "u64 x = bits_per(1);" HAVE_LOG2_BITS_PER "")

try_compile(
  "
#include <rdma/ib_umem.h>
  "
  "
struct ib_umem *umem;
struct sg_append_table sgt = umem->sgt_append;
  "
  HAVE_IB_UMEM_SGT_APPEND "")

try_compile(
  "
#include <rdma/ib_verbs.h>
  "
  "
struct ib_block_iter *iter;
size_t nb = iter->__sg_numblocks;
  "
  HAVE_IB_BLOCK_ITER_SG_NUM_BLOCKS "")

try_compile(
  "
#include <rdma/uverbs_ioctl.h>
  "
  "
struct uverbs_attr_bundle *attrs;
attrs = rdma_udata_to_uverbs_attr_bundle(NULL);
  "
  HAVE_UDATA_TO_UVERBS_ATTR_BUNDLE "")

try_compile(
  "
#include <rdma/uverbs_ioctl.h>
  "
  "
int fd;
fd = uverbs_get_raw_fd(NULL, NULL, 0);
  "
  HAVE_UVERBS_ATTR_RAW_FD "")

try_compile(
  "
#include <rdma/ib_umem.h>
  "
  "
struct ib_umem *umem;
dma_addr_t addr;

addr = ib_umem_start_dma_addr(umem);
  "
  HAVE_IB_UMEM_START_DMA_ADDR "")

try_compile(
  "
#include <rdma/ib_umem.h>
  "
  "
struct ib_umem *umem;
bool contiguous;

contiguous = ib_umem_is_contiguous(umem);
  "
  HAVE_IB_UMEM_IS_CONTIGUOUS "")

wait_for_pids()
message("-- Inspecting kernel - done")
