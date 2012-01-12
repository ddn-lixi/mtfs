/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@163.com>
 */
#include "hrfs_internal.h"

int hrfs_flag_invalidate_data(struct inode *inode, hrfs_bindex_t bindex)
{
	hrfs_bindex_t ret = -1;
	struct lowerfs_operations *lowerfs_ops = NULL;
	struct inode *hidden_inode = NULL;

	lowerfs_ops = hrfs_i2bops(inode, bindex);
	hidden_inode = hrfs_i2branch(inode, bindex);
	HASSERT(lowerfs_ops);
	HASSERT(hidden_inode);
	ret = lowerfs_inode_invalidate_data(lowerfs_ops, hidden_inode);

	return ret;	
}

int hrfs_flag_invalidate_attr(struct inode *inode, hrfs_bindex_t bindex)
{
	return 0;
}

int hrfs_get_branch_flag(struct inode *inode, hrfs_bindex_t bindex, __u32 *hrfs_flag)
{
	struct inode *hidden_inode = hrfs_i2branch(inode, bindex);
	struct lowerfs_operations *lowerfs_ops = hrfs_i2bops(inode, bindex);
	int ret = 0;

	HASSERT(hidden_inode);
	if (lowerfs_ops->lowerfs_inode_get_flag) {
		ret = lowerfs_ops->lowerfs_inode_get_flag(hidden_inode, hrfs_flag);
	}
	return ret;
}

int hrfs_branch_is_valid(struct inode *inode, hrfs_bindex_t bindex, __u32 valid_flags)
{
	int is_valid = 1;
	__u32 hrfs_flag = 0;
	int ret = 0;

	if (hrfs_i2branch(inode, bindex) == NULL) {
		is_valid = 0;
		goto out;
	}
	
	if (valid_flags == HRFS_BRANCH_VALID) {
		is_valid = 1;
		goto out;
	}

	ret = hrfs_get_branch_flag(inode, bindex, &hrfs_flag);
	if (ret) {
		HERROR("failed to get branch flag, ret = %d\n", ret);
		is_valid = ret;
		goto out;
	}

	if ((valid_flags & HRFS_DATA_VALID) != 0 &&
	    (hrfs_flag & HRFS_FLAG_DATABAD) != 0) {
		is_valid = 0;
	}
	/* TODO: xattr and attr */
out:
	return is_valid;
}
EXPORT_SYMBOL(hrfs_branch_is_valid);