# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

AC_DEFUN([EFA_CONFIG_RDMA],
[
	EFA_PARALLEL_INIT_ONCE

	AC_MSG_CHECKING([if ib umem scatterlist exists])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		struct ib_umem_chunk chunk;
	], [
		AC_MSG_RESULT(yes)
	], [
		AC_MSG_RESULT(no)
		AC_DEFINE(HAVE_UMEM_SCATTERLIST_IF, 1, ib umem scatterlist exists)
	])

	AC_MSG_CHECKING([if get_port_immutable exists])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		get_port_immutable
	], [
		efa_get_port_immutable
	], [
		int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num, struct ib_port_immutable *immutable) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_PORT_IMMUTABLE, 1, get_port_immutable exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if create_ah has udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		create_ah
	], [
		efa_kzalloc_ah
	], [
		struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd, struct ib_ah_attr *ah_attr, struct ib_udata *udata) { return NULL; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_AH_UDATA, 1, create_ah has udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if query_device has udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		query_device
	], [
		efa_query_device
	], [
		int efa_query_device(struct ib_device *ibdev, struct ib_device_attr *props, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_QUERY_DEVICE_UDATA, 1, query_device has udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if create_cq has attr param])
	EFA_TRY_COMPILE([
	], [
		struct ib_cq_init_attr attr;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_CQ_ATTR, 1, create_cq has attr param)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have hw_stats])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		alloc_hw_stats
	], [
		efa_alloc_hw_stats
	], [
		struct rdma_hw_stats *efa_alloc_hw_stats(struct ib_device *ibdev, u8 port_num) { return NULL; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_HW_STATS, 1, have hw_stats)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if create_ah has rdma_attr])
	EFA_TRY_COMPILE([
	], [
		struct rdma_ah_attr attr;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_AH_RDMA_ATTR, 1, create_ah has rdma_attr)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if dev has parent field])
	EFA_TRY_COMPILE([
	], [
		struct device dev = {
			.dma_ops = NULL,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEV_PARENT, 1, dev has parent field)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if rhel dev has parent field])
	EFA_TRY_COMPILE([
	], [
		struct device_rh dev = {
			.dma_ops = NULL,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEV_PARENT, 1, dev has parent field)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have const wr in post verbs])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		post_send
	], [
		efa_post_send
	], [
		static int efa_post_send(struct ib_qp *ibqp,
					const struct ib_send_wr *wr,
					const struct ib_send_wr **bad_wr)
		{ return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POST_CONST_WR, 1, have const wr in post verbs)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_device_attr has max_send_recv_sge])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_attr attr = {
			.max_send_sge = 0,
			.max_recv_sge = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAX_SEND_RCV_SGE, 1, ib_device_attr has max_send_recv_sge)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_register_device has name param])
	EFA_TRY_COMPILE([
	], [
		char name[2];
		ib_register_device(NULL, name, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_REGISTER_DEVICE_NAME_PARAM, 1, ib_register_device has name param)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_modify_qp_is_ok has four params])
	EFA_TRY_COMPILE([
	], [
		ib_modify_qp_is_ok(0, 0, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_MODIFY_QP_IS_OK_FOUR_PARAMS, 1, ib_modify_qp_is_ok has four params)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if rdma_user_mmap_io exists])
	EFA_TRY_COMPILE([
	], [
		pgprot_t prot;
		rdma_user_mmap_io(NULL, NULL, 0, 0, prot);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RDMA_USER_MMAP_IO, 1, rdma_user_mmap_io exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if struct ib_device_ops exists])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_ops ops;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_DEV_OPS, 1, struct ib_device_ops exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if create/destroy_ah has flags])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		create_ah
	], [
		efa_kzalloc_ah
	], [
		struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd, struct rdma_ah_attr *ah_attr, u32 flags, struct ib_udata *udata) { return NULL; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_DESTROY_AH_FLAGS, 1, create/destroy_ah has flags)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if for_each_sg_dma_page exists])
	EFA_TRY_COMPILE([
		#include <linux/scatterlist.h>
		#include <rdma/ib_umem.h>
	], [
		int i;
		struct ib_umem *umem;
		struct sg_dma_page_iter *sg_iter;
		for_each_sg_dma_page(umem->sg_head.sgl, sg_iter, umem->nmap, 0)
			i++;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SG_DMA_PAGE_ITER, 1, for_each_sg_dma_page exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have pd core allocation])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_ops ops = {
			.size_ib_pd = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PD_CORE_ALLOCATION, 1, have pd core allocation)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ucontext core allocation])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_ops ops = {
			.size_ib_ucontext = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_UCONTEXT_CORE_ALLOCATION, 1, have ucontext core allocation)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have no kverbs drivers])
	EFA_TRY_COMPILE([
	], [
		struct ib_device dev = {
			.kverbs_provider = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NO_KVERBS_DRIVERS, 1, have no kverbs drivers)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_umem_get has udata])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		struct ib_udata *udata;
		ib_umem_get(udata, 0, 0, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_UMEM_GET_UDATA, 1, ib_umem_get has udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if rdma_udata_to_drv_context exists])
	EFA_TRY_COMPILE([
		#include <rdma/uverbs_ioctl.h>

		struct efa_ucontext {
			struct ib_ucontext ibucontext;
		};
	], [
		struct ib_udata *udata;
		rdma_udata_to_drv_context(udata, struct efa_ucontext, ibucontext);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_UDATA_TO_DRV_CONTEXT, 1, rdma_udata_to_drv_context exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_register_device has two params])
	EFA_TRY_COMPILE([
	], [
		struct ib_device *dev;
		char name[2];
		ib_register_device(dev, name);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_REGISTER_DEVICE_TWO_PARAMS, 1, ib_register_device has two params)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if safe ib_alloc_device exists])
	EFA_TRY_COMPILE([

		struct efa_dev {
			struct ib_device ibdev;
		};
	], [
		ib_alloc_device(efa_dev, ibdev);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SAFE_IB_ALLOC_DEVICE, 1, safe ib_alloc_device exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ah core allocation])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_ops ops = {
			.size_ib_ah = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AH_CORE_ALLOCATION, 1, have ah core allocation)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops alloc_pd without ucontext])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		alloc_pd
	], [
		efa_alloc_pd
	], [
		int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ALLOC_PD_NO_UCONTEXT, 1, have device ops alloc_pd without ucontext)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops create_cq without ucontext])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		create_cq
	], [
		efa_kzalloc_cq
	], [
		struct ib_cq *efa_kzalloc_cq(struct ib_device *ibdev, const struct ib_cq_init_attr *attr, struct ib_udata *udata) { return NULL; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_CQ_NO_UCONTEXT, 1, have device ops create_cq without ucontext)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops dealloc pd has udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		dealloc_pd
	], [
		efa_dealloc_pd
	], [
		void efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) {}
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEALLOC_PD_UDATA, 1, have device ops dealloc pd has udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops dereg mr udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		dereg_mr
	], [
		efa_dereg_mr
	], [
		int efa_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEREG_MR_UDATA, 1, have device ops dereg mr udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops destroy cq udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		destroy_cq
	], [
		efa_destroy_cq
	], [
		int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DESTROY_CQ_UDATA, 1, have device ops destroy cq udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have device ops destroy qp udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		destroy_qp
	], [
		efa_destroy_qp
	], [
		int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DESTROY_QP_UDATA, 1, have device ops destroy qp udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_umem_find_single_pg_size exists])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		ib_umem_find_best_pgsz(NULL, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_UMEM_FIND_SINGLE_PG_SIZE, 1, ib_umem_find_single_pg_size exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have upstream efa])
	EFA_TRY_COMPILE([
	], [
		enum rdma_driver_id driver_id = RDMA_DRIVER_EFA;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_UPSTREAM_EFA, 1, have upstream efa)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_device_ops has common fields])
	EFA_TRY_COMPILE([
	], [
		struct ib_device_ops ops = {
			.driver_id = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_DEVICE_OPS_COMMON, 1, ib_device_ops has common fields)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have void destroy cq])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		destroy_cq
	], [
		efa_destroy_cq
	], [
		void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) {}
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_VOID_DESTROY_CQ, 1, have void destroy cq)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have cq core allocation])
	EFA_TRY_COMPILE([

		void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) {}
	], [
		struct ib_device_ops ops = {
			.size_ib_cq = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CQ_CORE_ALLOCATION, 1, have cq core allocation)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ib port phys state link up])
	EFA_TRY_COMPILE([

		void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) {}
	], [
		int a = IB_PORT_PHYS_STATE_LINK_UP;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_PORT_PHYS_STATE_LINK_UP, 1, have ib port phys state link up)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have kvzalloc])
	EFA_TRY_COMPILE([
		#include <linux/mm.h>
	], [
		kvzalloc(0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KVZALLOC, 1, have kvzalloc)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ibdev ratelimited print])
	EFA_TRY_COMPILE([
	], [
		struct ib_device *dev;
		ibdev_err_ratelimited(dev, \"Hello\n\");
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IBDEV_PRINT_RATELIMITED, 1, have ibdev ratelimited print)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ibdev print])
	EFA_TRY_COMPILE([
	], [
		struct ib_device *dev;
		char fmt[2];
		ibdev_err(dev, \"World\n\");
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IBDEV_PRINT, 1, have ibdev print)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have driver qpt])
	EFA_TRY_COMPILE([
	], [
		int a = IB_QPT_DRIVER;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_QPT_DRIVER, 1, have driver qpt)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ib_is_udata_cleared])
	EFA_TRY_COMPILE([
	], [
		ib_is_udata_cleared(NULL, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_IS_UDATA_CLEARED, 1, have ib_is_udata_cleared)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if create_ah doesn't have udata])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		create_ah
	], [
		efa_kzalloc_ah
	], [
		struct ib_ah *efa_kzalloc_ah(struct ib_pd *ibpd, struct ib_ah_attr *ah_attr) { return NULL; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_AH_NO_UDATA, 1, create_ah doesn't have udata)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if driver_id field exists])
	EFA_TRY_COMPILE([
	], [
		struct ib_device dev = {
			.driver_id = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRIVER_ID, 1, driver_id field exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_mr has length field])
	EFA_TRY_COMPILE([
	], [
		struct ib_mr mr = {
			.length = 0,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_MR_LENGTH, 1, ib_mr has length field)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_mtu_int_to_enum exists])
	EFA_TRY_COMPILE([
	], [
		ib_mtu_int_to_enum(0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_MTU_INT_TO_ENUM, 1, ib_mtu_int_to_enum exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have uverbs command header fix])
	EFA_TRY_COMPILE([
	], [
		struct ib_device dev = {
			.specs_root = NULL,
		};
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_UVERBS_CMD_HDR_FIX, 1, have uverbs command header fix)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have amazon pci id])
	EFA_TRY_COMPILE([
		#include <linux/pci_ids.h>
	], [
		int a = PCI_VENDOR_ID_AMAZON;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PCI_VENDOR_ID_AMAZON, 1, have amazon pci id)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have pci_irq_vector])
	EFA_TRY_COMPILE([
		#include <linux/pci.h>
	], [
		pci_irq_vector(NULL, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PCI_IRQ_VECTOR, 1, have pci_irq_vector)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if ib_umem_get has no dmasync parameter])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		ib_umem_get(NULL, 0, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_UMEM_GET_NO_DMASYNC, 1, ib_umem_get has no dmasync parameter)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have core mmap xarray])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		pgprot_t pgt;
		rdma_user_mmap_io(NULL, NULL, 0, 0, pgt, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CORE_MMAP_XA, 1, have core mmap xarray)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have unspecified node type])
	EFA_TRY_COMPILE([
	], [
		int a = RDMA_NODE_UNSPECIFIED;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RDMA_NODE_UNSPECIFIED, 1, have unspecified node type)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have bitfield.h])
	EFA_TRY_COMPILE([
		#include <linux/bitfield.h>
	], [
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BITFIELD_H, 1, have bitfield.h)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if have ib_umem_get device param])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		struct ib_device *dev;
		ib_umem_get(dev, 0, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_UMEM_GET_DEVICE_PARAM, 1, have ib_umem_get device param)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if IB_ACCESS_OPTIONAL exists])
	EFA_TRY_COMPILE([
	], [
		int a = IB_ACCESS_OPTIONAL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_ACCESS_OPTIONAL, 1, IB_ACCESS_OPTIONAL exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if rdma_ah_init_attr exists])
	EFA_TRY_COMPILE([
	], [
		struct rdma_ah_init_attr ah_attr;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_AH_INIT_ATTR, 1, rdma_ah_init_attr exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if atomic64_fetch_inc exists])
	EFA_TRY_COMPILE([
	], [
		atomic64_fetch_inc(NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ATOMIC64_FETCH_INC, 1, atomic64_fetch_inc exists)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if dealloc_pd has udata and return code])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		dealloc_pd
	], [
		efa_dealloc_pd
	], [
		int efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEALLOC_PD_UDATA_RC, 1, dealloc_pd has udata and return code)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if destroy_ah has return code again])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		destroy_ah
	], [
		efa_destroy_ah
	], [
		int efa_destroy_ah(struct ib_ah *ibah, u32 flags) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AH_CORE_ALLOCATION_DESTROY_RC, 1, destroy_ah has return code again)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if destroy_cq has return code again])
	EFA_TRY_COMPILE_DEV_OR_OPS_FUNC([
		destroy_cq
	], [
		efa_destroy_cq
	], [
		int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata) { return 0; }
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IB_INT_DESTROY_CQ, 1, destroy_cq has return code again)
	], [
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([if rdma_umem_for_each_dma_block exists])
	EFA_TRY_COMPILE([
		#include <rdma/ib_umem.h>
	], [
		struct ib_block_iter biter;
		struct ib_umem *umem;
		int i;

		rdma_umem_for_each_dma_block(umem, &biter, 0)
			i++;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RDMA_UMEM_FOR_EACH_DMA_BLOCK, 1, rdma_umem_for_each_dma_block exists)
	], [
		AC_MSG_RESULT(no)
	])

	wait
])
