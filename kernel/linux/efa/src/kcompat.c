// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "kcompat.h"

#ifdef HAVE_IB_DEVICE_DRIVER_DEF
#include <rdma/uverbs_ioctl.h>
#define UVERBS_MODULE_NAME efa_ib
#include <rdma/uverbs_named_ioctl.h>
#endif
#include "efa.h"
#include "efa-abi.h"

#ifdef HAVE_IB_DEVICE_DRIVER_DEF
#ifndef HAVE_IB_COMP_CNTR
static int UVERBS_HANDLER(UVERBS_METHOD_QUERY_COMP_CNTR_CAPS)(struct uverbs_attr_bundle *attrs)
{
	struct ib_ucontext *ucontext;
	struct ib_comp_cntr_caps caps = {};
	struct ib_device *ibdev;
	int ret;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);

	ibdev = ucontext->device;

	ret = efa_query_comp_cntr_caps(ibdev, &caps, attrs);
	if (ret)
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_MAX_COUNTERS,
			     &caps.max_counters, sizeof(caps.max_counters));
	if (IS_UVERBS_COPY_ERR(ret))
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_MAX_VALUE,
			     &caps.max_value, sizeof(caps.max_value));
	if (IS_UVERBS_COPY_ERR(ret))
		return ret;

	ret = uverbs_copy_to(attrs, UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_SUPPORTED_QP_ATTACH_OPS,
			     &caps.supported_qp_attach_ops,
			     sizeof(caps.supported_qp_attach_ops));
	return IS_UVERBS_COPY_ERR(ret) ? ret : 0;
}

static int efa_free_comp_cntr(struct ib_uobject *uobject, enum rdma_remove_reason why,
			      struct uverbs_attr_bundle *attrs)
{
	struct efa_comp_cntr *cc = uobject->object;

	if (atomic_read(&cc->ibcc.usecnt))
		return -EBUSY;

	efa_destroy_comp_cntr(&cc->ibcc);
	kfree(cc);
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_CREATE)(struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
							  UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE);
	struct ib_ucontext *ucontext = ib_uverbs_get_ucontext(attrs);
	struct ib_device *ib_dev;
	struct efa_comp_cntr *cc;
	int ret;

	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);
	ib_dev = ucontext->device;

	cc = kzalloc(sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->ibcc.device = ib_dev;
	cc->ibcc.uobject = uobj;

	ret = efa_create_comp_cntr(&cc->ibcc, attrs);
	if (ret) {
		kfree(cc);
		return ret;
	}

	uobj->object = cc;
#ifdef HAVE_UVERBS_FINALIZE_UOBJ_CREATE
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE);
#endif
	return 0;
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_MODIFY)(struct uverbs_attr_bundle *attrs)
{
	struct efa_comp_cntr *cc = uverbs_attr_get_obj(attrs,
						       UVERBS_ATTR_MODIFY_COMP_CNTR_HANDLE);
	enum ib_comp_cntr_modify_op op;
	enum ib_comp_cntr_entry entry;
	u64 value;
	int ret;

	ret = uverbs_get_const(&entry, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_ENTRY);
	if (ret)
		return ret;

	ret = uverbs_get_const(&op, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_OP);
	if (ret)
		return ret;

	ret = uverbs_copy_from(&value, attrs, UVERBS_ATTR_MODIFY_COMP_CNTR_VALUE);
	if (ret)
		return ret;

	return efa_modify_comp_cntr(&cc->ibcc, entry, op, value);
}

static int UVERBS_HANDLER(UVERBS_METHOD_COMP_CNTR_READ)(struct uverbs_attr_bundle *attrs)
{
	return -EOPNOTSUPP;
}

static int UVERBS_HANDLER(UVERBS_METHOD_QP_ATTACH_COMP_CNTR)(struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *qp_uobj = uverbs_attr_get_uobject(attrs,
					UVERBS_ATTR_QP_ATTACH_COMP_CNTR_HANDLE);
	struct efa_comp_cntr *cc = uverbs_attr_get_obj(attrs,
					UVERBS_ATTR_QP_ATTACH_COMP_CNTR_CNTR_HANDLE);
	struct ib_qp_attach_comp_cntr_attr attr = {};
	struct efa_qp *qp;
	int ret;

	qp = container_of((struct ib_qp *)qp_uobj->object, struct efa_qp, ibqp);

	ret = uverbs_get_flags32(&attr.op_mask, attrs,
				 UVERBS_ATTR_QP_ATTACH_COMP_CNTR_OP_MASK,
				 IB_QP_ATTACH_COMP_CNTR_OP_SEND |
				 IB_QP_ATTACH_COMP_CNTR_OP_RECV |
				 IB_QP_ATTACH_COMP_CNTR_OP_RDMA_READ |
				 IB_QP_ATTACH_COMP_CNTR_OP_REMOTE_RDMA_READ |
				 IB_QP_ATTACH_COMP_CNTR_OP_RDMA_WRITE |
				 IB_QP_ATTACH_COMP_CNTR_OP_REMOTE_RDMA_WRITE);
	if (ret)
		return ret;

	if (!attr.op_mask)
		return -EINVAL;

	if (attr.op_mask & qp->comp_cntr_op_mask)
		return -EBUSY;

#ifdef HAVE_XARRAY
	ret = xa_err(xa_store(&qp->comp_cntrs, attr.op_mask, &cc->ibcc, GFP_KERNEL));
	if (ret)
		return ret;
#endif

	ret = efa_qp_attach_comp_cntr(&qp->ibqp, &cc->ibcc, &attr);
	if (ret) {
#ifdef HAVE_XARRAY
		xa_erase(&qp->comp_cntrs, attr.op_mask);
#endif
		return ret;
	}

#ifdef HAVE_XARRAY
	atomic_inc(&cc->ibcc.usecnt);
#endif
	qp->comp_cntr_op_mask |= attr.op_mask;

	return 0;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_QUERY_COMP_CNTR_CAPS,
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_MAX_COUNTERS,
			    UVERBS_ATTR_TYPE(u32),
			    UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_MAX_VALUE,
			    UVERBS_ATTR_TYPE(u64),
			    UA_OPTIONAL),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_QUERY_COMP_CNTR_CAPS_SUPPORTED_QP_ATTACH_OPS,
			    UVERBS_ATTR_TYPE(u32),
			    UA_OPTIONAL));

ADD_UVERBS_METHODS(efa_device_comp_cntr_caps,
		   UVERBS_OBJECT_DEVICE,
		   &UVERBS_METHOD(UVERBS_METHOD_QUERY_COMP_CNTR_CAPS));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_CREATE,
	UVERBS_ATTR_IDR(UVERBS_ATTR_CREATE_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	UVERBS_METHOD_COMP_CNTR_DESTROY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_DESTROY_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_MODIFY,
	UVERBS_ATTR_IDR(UVERBS_ATTR_MODIFY_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_WRITE,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_ENTRY,
			     enum ib_uverbs_comp_cntr_entry,
			     UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_OP,
			     enum ib_uverbs_comp_cntr_modify_op,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_MODIFY_COMP_CNTR_VALUE,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_COMP_CNTR_READ,
	UVERBS_ATTR_IDR(UVERBS_ATTR_READ_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(UVERBS_ATTR_READ_COMP_CNTR_ENTRY,
			     enum ib_uverbs_comp_cntr_entry,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_READ_COMP_CNTR_RESP_VALUE,
			    UVERBS_ATTR_TYPE(u64),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_COMP_CNTR,
	UVERBS_TYPE_ALLOC_IDR(efa_free_comp_cntr),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_CREATE),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_DESTROY),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_MODIFY),
	&UVERBS_METHOD(UVERBS_METHOD_COMP_CNTR_READ));

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_QP_ATTACH_COMP_CNTR,
	UVERBS_ATTR_IDR(UVERBS_ATTR_QP_ATTACH_COMP_CNTR_HANDLE,
			UVERBS_OBJECT_QP,
			UVERBS_ACCESS_WRITE,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(UVERBS_ATTR_QP_ATTACH_COMP_CNTR_CNTR_HANDLE,
			UVERBS_OBJECT_COMP_CNTR,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_QP_ATTACH_COMP_CNTR_OP_MASK,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY));

ADD_UVERBS_METHODS(efa_qp_comp_cntr,
		   UVERBS_OBJECT_QP,
		   &UVERBS_METHOD(UVERBS_METHOD_QP_ATTACH_COMP_CNTR));
#endif /* !HAVE_IB_COMP_CNTR */

const struct uapi_definition efa_kcompat_uapi_defs[] = {
#ifndef HAVE_IB_COMP_CNTR
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_DEVICE,
				&efa_device_comp_cntr_caps),
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_COMP_CNTR,
				&UVERBS_OBJECT(UVERBS_OBJECT_COMP_CNTR)),
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_QP,
				&efa_qp_comp_cntr),
#endif
	{},
};

#endif
