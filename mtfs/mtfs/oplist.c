/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <memory.h>
#include <mtfs_oplist.h>
#include <mtfs_inode.h>
#include <mtfs_flag.h>
#include <linux/module.h>
#include "main_internal.h"

static int mtfs_oplist_init_flag(struct mtfs_operation_list *oplist, struct inode *inode)
{
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t bnum = mtfs_i2bnum(inode);
	mtfs_bindex_t blast = bnum - 1;
	mtfs_bindex_t bfirst = 0;
	int is_valid = 1;
	struct mtfs_operation_binfo *binfo = NULL;
	int ret = 0;
	MENTRY();

	/* NOT a good implementation, change me */
	for (bindex = 0; bindex < bnum; bindex++) {
		is_valid = mtfs_branch_valid(inode, bindex, MTFS_DATA_VALID);
		if (is_valid) {
			binfo = &(oplist->op_binfo[bfirst]);
			bfirst++;
		} else {
			binfo = &(oplist->op_binfo[blast]);
			blast--;
		}
		binfo->bindex = bindex;
	}
	oplist->latest_bnum = bfirst;
	MRETURN(ret);
}

static int mtfs_oplist_flush_invalidate(struct mtfs_operation_list *oplist,
                              struct inode *inode)
{
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t i = 0;
	int ret = 0;
	MENTRY();

	/*
	 * Degradate only if some of latest branches failed
	 * and others succeeded 
	 */
	if (oplist->success_latest_bnum == 0 ||
	    oplist->fault_latest_bnum == 0) {
		goto out;
	}

	/*
	 * During write_ops, some non-latest branches
	 * may be skipped and not valid. 
	 */
	for (i = 0; i < oplist->valid_bnum; i++) {
		bindex = oplist->op_binfo[i].bindex;
		MASSERT(oplist->op_binfo[i].valid);

		if (!oplist->op_binfo[i].flags & MTFS_OPERATION_SUCCESS) {
			if (mtfs_i2branch(inode, bindex) == NULL) {
				continue;
			}

			MTRACE("invalidating %d\n", bindex);
			ret = mtfs_branch_invalidate(inode, bindex, MTFS_DATA_VALID);
			if (ret) {
				MERROR("invalid inode failed, ret = %d\n", ret);
				MBUG();
			}
		}
	}
out:
	MRETURN(ret);
}

static int mtfs_oplist_gather_optimistic(struct mtfs_operation_list *oplist)
{
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t bindex_chosed = -1;
	int ret = 0;
	MENTRY();

	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);

	for (bindex = 0; bindex < oplist->valid_bnum; bindex++) {
		MASSERT(oplist->op_binfo[bindex].valid);
		if (oplist->op_binfo[bindex].flags & MTFS_OPERATION_PREFERABLE) {
			MASSERT(oplist->op_binfo[bindex].flags & MTFS_OPERATION_SUCCESS);
			bindex_chosed = bindex;
			break;
		}
	}

	if (bindex_chosed != -1) {
		oplist->opinfo = &(oplist->op_binfo[bindex_chosed]);
	} else {
		MASSERT(oplist->valid_bnum > 0);
		if (!oplist->fault_bnum) {
			MASSERT(!(oplist->op_binfo[0].flags & MTFS_OPERATION_PREFERABLE));
		}
		MASSERT(oplist->op_binfo[0].valid);
		oplist->opinfo = &(oplist->op_binfo[0]);
	}
	MASSERT(oplist->opinfo);

	MRETURN(ret);
}

struct mtfs_operation_list *mtfs_oplist_alloc(mtfs_bindex_t bnum)
{
	struct mtfs_operation_list *oplist = NULL;

	MASSERT(bnum > 0 && bnum <= MTFS_BRANCH_MAX);
	MTFS_SLAB_ALLOC_PTR(oplist, mtfs_oplist_cache);	
	if (unlikely(oplist == NULL)) {
		goto out;
	}
	oplist->bnum = bnum;

out:
	return oplist;
}
EXPORT_SYMBOL(mtfs_oplist_alloc);

void mtfs_oplist_free(struct mtfs_operation_list *oplist)
{
	MTFS_SLAB_FREE_PTR(oplist, mtfs_oplist_cache);
}
EXPORT_SYMBOL(mtfs_oplist_free);

static inline void mtfs_oplist_dump(struct mtfs_operation_list *oplist)
{
	mtfs_bindex_t bindex = 0;

	MPRINT("oplist bnum = %d\n", oplist->bnum);
	MPRINT("oplist latest_bnum = %d\n", oplist->latest_bnum);
	for (bindex = 0; bindex < oplist->bnum; bindex++) {
		MPRINT("branch[%d]: %d\n", bindex, oplist->op_binfo[bindex].bindex);
	}
}

struct mtfs_operation_list *mtfs_oplist_build(struct inode *inode,
                                              struct mtfs_oplist_object *oplist_obj)
{
	struct mtfs_operation_list *oplist = NULL;
	mtfs_bindex_t bnum = mtfs_i2bnum(inode);
	int ret = 0;
	MENTRY();
	
	oplist = mtfs_oplist_alloc(bnum);
	if (unlikely(oplist == NULL)) {
		goto out;
	}

	ret = mtfs_oplist_init(oplist, inode, oplist_obj);
	if (unlikely(ret)) {
		mtfs_oplist_free(oplist);
		oplist = NULL;
	}	
out:
	MRETURN(oplist);
}
EXPORT_SYMBOL(mtfs_oplist_build);

static int mtfs_oplist_init_sequential(struct mtfs_operation_list *list, struct inode *inode)
{
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t bnum = mtfs_i2bnum(inode);
	int ret = 0;
	MENTRY();

	for (bindex = 0; bindex < bnum; bindex++) {
	        list->op_binfo[bindex].bindex = bindex;
	}
	list->latest_bnum = bnum;

	MRETURN(ret);
}

static int mtfs_oplist_init_reverse(struct mtfs_operation_list *list, struct inode *inode)
{
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t bnum = mtfs_i2bnum(inode);
	int ret = 0;
	MENTRY();

	for (bindex = 0; bindex < bnum; bindex++) {
	        list->op_binfo[bindex].bindex = bnum - bindex - 1;
	}
	list->latest_bnum = bnum;

	MRETURN(ret);
}

int mtfs_oplist_init(struct mtfs_operation_list *oplist,
                     struct inode *inode,
                     struct mtfs_oplist_object *oplist_obj)
{
	int ret = 0;
	MENTRY();

	MASSERT(oplist_obj->mopo_init);
	ret = oplist_obj->mopo_init(oplist, inode);
	if (ret) {
		MERROR("failed to init oplist\n");
		goto out;
	}
	oplist->bnum = mtfs_i2bnum(inode);
	oplist->mopl_type = oplist_obj;
	oplist->inited = 1;
out:
	MRETURN(ret);
}
EXPORT_SYMBOL(mtfs_oplist_init);

int mtfs_oplist_flush(struct mtfs_operation_list *oplist,
                      struct inode *inode)
{
	int ret = 0;
	MENTRY();

	if (oplist->mopl_type->mopo_flush) {
		ret = oplist->mopl_type->mopo_flush(oplist, inode);
		if (ret) {
			MERROR("failed to flush oplist\n");
		}
	}

	MRETURN(ret);
}
EXPORT_SYMBOL(mtfs_oplist_flush);

int mtfs_oplist_setbranch(struct mtfs_operation_list *oplist,
                          mtfs_bindex_t bindex,
                          __u32 flags,
                          mtfs_operation_result_t result)
{
	MASSERT(bindex >= 0 && bindex < oplist->bnum);
	oplist->op_binfo[bindex].valid = 1;
	oplist->op_binfo[bindex].flags = flags;
	oplist->op_binfo[bindex].result = result;

	oplist->valid_bnum++;

	if (oplist->op_binfo[bindex].flags & MTFS_OPERATION_SUCCESS) {
		oplist->success_bnum++;
		if (bindex < oplist->latest_bnum) {
			oplist->success_latest_bnum++;
		} else {
			oplist->success_nonlatest_bnum++;
		}
	} else {
		oplist->fault_bnum++;
		if (bindex < oplist->latest_bnum) {
			oplist->fault_latest_bnum++;
		} else {
			oplist->fault_nonlatest_bnum++;
		}
	}
	return 0;
}
EXPORT_SYMBOL(mtfs_oplist_setbranch);

/* Choose a operation status returned */
int mtfs_oplist_gather(struct mtfs_operation_list *oplist)
{
	int ret = 0;
	MENTRY();

	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);

	MASSERT(oplist->mopl_type->mopo_gather);
	ret = oplist->mopl_type->mopo_gather(oplist);
	if (ret) {
		MERROR("failed to gather oplist\n");
	}

	MRETURN(ret);
}
EXPORT_SYMBOL(mtfs_oplist_gather);

/* Choose a operation status returned */
mtfs_operation_result_t mtfs_oplist_result(struct mtfs_operation_list *oplist)
{
	MASSERT(oplist->opinfo);
	return oplist->opinfo->result;
}
EXPORT_SYMBOL(mtfs_oplist_result);

static int mtfs_oplist_gather_master(struct mtfs_operation_list *oplist)
{
	int ret = 0;
	MENTRY();

	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);

	oplist->opinfo = &(oplist->op_binfo[0]);
	MASSERT(oplist->opinfo->valid);

	MRETURN(ret);
}

static int mtfs_oplist_gather_reverse(struct mtfs_operation_list *oplist)
{
	int ret = 0;
	MENTRY();

	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);

	oplist->opinfo = &(oplist->op_binfo[oplist->bnum - 1]);
	MASSERT(oplist->opinfo->valid);

	MRETURN(ret);
}

static int mtfs_oplist_gather_writev(struct mtfs_operation_list *oplist)
{
	mtfs_bindex_t bindex = 0;
	int ret = 0;
	ssize_t max_size = 0;
	mtfs_bindex_t max_bindex = -1;
	mtfs_bindex_t valid_bnum = oplist->valid_bnum;
	MENTRY();

	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);
	MASSERT(oplist->valid_bnum > 0);

	for (bindex = 0; bindex < valid_bnum; bindex++) {
		MASSERT(oplist->op_binfo[bindex].valid);

		if (max_size < oplist->op_binfo[bindex].result.ssize){
			max_size = oplist->op_binfo[bindex].result.ssize;
			max_bindex = bindex;
		}

		if (oplist->op_binfo[bindex].flags & MTFS_OPERATION_PREFERABLE) {
			MASSERT(oplist->op_binfo[bindex].flags & MTFS_OPERATION_SUCCESS);
			MASSERT(max_size == oplist->op_binfo[bindex].result.ssize);
			break;
		}
	}

	oplist->valid_bnum = 0;
	oplist->success_bnum = 0;
	oplist->success_latest_bnum = 0;
	oplist->success_nonlatest_bnum = 0;
	oplist->fault_bnum = 0;
	oplist->fault_latest_bnum = 0;
	oplist->fault_nonlatest_bnum = 0;
	for (bindex = 0; bindex < valid_bnum; bindex++) {
		MASSERT(max_size >= oplist->op_binfo[bindex].result.ssize);
		if (max_size == oplist->op_binfo[bindex].result.ssize) {
			mtfs_oplist_setbranch(oplist, bindex,
			                      MTFS_OPERATION_SUCCESS | MTFS_OPERATION_PREFERABLE,
			                      oplist->op_binfo[bindex].result);
		} else {
			mtfs_oplist_setbranch(oplist, bindex,
			                      0,
			                      oplist->op_binfo[bindex].result);
		}
	}
	MASSERT(oplist->fault_latest_bnum + oplist->fault_nonlatest_bnum == oplist->fault_bnum);
	MASSERT(oplist->success_latest_bnum + oplist->success_nonlatest_bnum == oplist->success_bnum);
	MASSERT(oplist->success_bnum + oplist->fault_bnum == oplist->valid_bnum);
	MASSERT(oplist->valid_bnum <= oplist->bnum);
	MASSERT(oplist->valid_bnum == valid_bnum);

	if (max_bindex == -1) {
		max_bindex = 0;
		MASSERT(oplist->success_bnum == 0);
	}
	oplist->opinfo = &(oplist->op_binfo[max_bindex]);

	MASSERT(oplist->opinfo);

	MRETURN(ret);
}

struct mtfs_oplist_object mtfs_oplist_flag = {
	.mopo_init     = mtfs_oplist_init_flag,
	.mopo_flush    = mtfs_oplist_flush_invalidate,
	.mopo_gather   = mtfs_oplist_gather_optimistic,
};
EXPORT_SYMBOL(mtfs_oplist_flag);

struct mtfs_oplist_object mtfs_oplist_sequential = {
	.mopo_init     = mtfs_oplist_init_sequential,
	.mopo_flush    = mtfs_oplist_flush_invalidate,
	.mopo_gather   = mtfs_oplist_gather_optimistic,
};
EXPORT_SYMBOL(mtfs_oplist_sequential);

struct mtfs_oplist_object mtfs_oplist_flag_writev = {
	.mopo_init     = mtfs_oplist_init_flag,
	.mopo_flush    = mtfs_oplist_flush_invalidate,
	.mopo_gather   = mtfs_oplist_gather_writev,
};
EXPORT_SYMBOL(mtfs_oplist_flag_writev);

struct mtfs_oplist_object mtfs_oplist_master = {
	.mopo_init     = mtfs_oplist_init_sequential,
	.mopo_flush    = NULL,
	.mopo_gather   = mtfs_oplist_gather_master,
};
EXPORT_SYMBOL(mtfs_oplist_master);

struct mtfs_oplist_object mtfs_oplist_reverse = {
	.mopo_init     = mtfs_oplist_init_reverse,
	.mopo_flush    = NULL,
	.mopo_gather   = mtfs_oplist_gather_reverse,
};
EXPORT_SYMBOL(mtfs_oplist_reverse);

struct mtfs_oplist_object mtfs_oplist_equal = {
	.mopo_init     = mtfs_oplist_init_sequential,
	.mopo_flush    = NULL,
	.mopo_gather   = mtfs_oplist_gather_optimistic,
};
EXPORT_SYMBOL(mtfs_oplist_equal);
