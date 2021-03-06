/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <linux/module.h>
#include <compat.h>
#include <mtfs_heal.h>
#include <mtfs_lowerfs.h>
#include <mtfs_super.h>
#include <mtfs_dentry.h>
#include <mtfs_inode.h>
#include <mtfs_file.h>
#include <mtfs_junction.h>
#include <mtfs_flag.h>
#include "lowerfs_ext.h"
#include "lowerfs_ext2.h"
#include "lowerfs_ext3.h"
#include "lowerfs_ext4.h"

typedef struct buffer_head *(* _mlowerfs_ext3_bread_t)(handle_t *handle,
                                                       struct inode *inode,
                                                       int block,
                                                       int create,
                                                       int *err);

_mlowerfs_ext3_bread_t _mlowerfs_ext3_bread_pointer = NULL;
struct module *_mlowerfs_ext3_bread_owner = NULL;

struct buffer_head *_mlowerfs_ext3_bread(handle_t *handle,
                                         struct inode *inode,
                                         int block,
                                         int create,
                                         int *err)
{
	MASSERT(_mlowerfs_ext3_bread_pointer);
	return _mlowerfs_ext3_bread_pointer(handle, inode, block, create, err);
}

typedef struct inode *(* _mlowerfs_ext3_iget_t)(struct super_block *sb,
						unsigned long ino);

_mlowerfs_ext3_iget_t _mlowerfs_ext3_iget_pointer = NULL;
struct module *_mlowerfs_ext3_iget_owner = NULL;

struct inode *_mlowerfs_ext3_iget(struct super_block *sb,
				  unsigned long ino)
{
	MASSERT(_mlowerfs_ext3_iget_pointer);
	return _mlowerfs_ext3_iget_pointer(sb, ino);
}

static int ext_support_init(void)
{
	int ret = 0;
	unsigned long address = 0;

	MDEBUG("registering mtfs lowerfs for ext\n");
	ret = mtfs_symbol_get("ext3", "ext3_bread", &address, &_mlowerfs_ext3_bread_owner);
	if (ret) {
		MERROR("failed to get address of symbple ext3_bread, ret = %d\n", ret);
		goto out;
	}
	MASSERT(_mlowerfs_ext3_bread_owner != NULL);
	_mlowerfs_ext3_bread_pointer = (_mlowerfs_ext3_bread_t)address;

	ret = mtfs_symbol_get("ext3", "ext3_iget", &address, &_mlowerfs_ext3_iget_owner);
	if (ret) {
		MERROR("failed to get address of symbple ext3_bread, ret = %d\n", ret);
		goto out_ext3_bread_put;
	}
	MASSERT(_mlowerfs_ext3_iget_owner != NULL);
	_mlowerfs_ext3_iget_pointer = (_mlowerfs_ext3_iget_t)address;

	ret = mlowerfs_register(&lowerfs_ext2);
	if (ret) {
		MERROR("failed to register lowerfs for ext2, ret = %d\n", ret);
		goto out_ext3_iget_put;
	}

	ret = mlowerfs_register(&lowerfs_ext3);
	if (ret) {
		MERROR("failed to register lowerfs for ext3, ret = %d\n", ret);
		goto out_unregister_ext2;
	}

	ret = mlowerfs_register(&lowerfs_ext4);
	if (ret) {
		MERROR("failed to register lowerfs for ext4, ret = %d\n", ret);
		goto out_unregister_ext3;
	}
	goto out;
out_unregister_ext3:
	mlowerfs_unregister(&lowerfs_ext3);
out_unregister_ext2:
	mlowerfs_unregister(&lowerfs_ext2);
out_ext3_iget_put:
	mtfs_symbol_put(_mlowerfs_ext3_iget_owner);
out_ext3_bread_put:
	mtfs_symbol_put(_mlowerfs_ext3_bread_owner);
out:
	return ret;
}

static void ext_support_exit(void)
{
	MDEBUG("unregistering mtfs lowerfs for ext\n");
	mlowerfs_unregister(&lowerfs_ext2);
	mlowerfs_unregister(&lowerfs_ext3);
	mlowerfs_unregister(&lowerfs_ext4);
	MASSERT(_mlowerfs_ext3_bread_owner);
	mtfs_symbol_put(_mlowerfs_ext3_bread_owner);
}

MODULE_AUTHOR("MulTi File System Workgroup");
MODULE_DESCRIPTION("mtfs's support for ext2");
MODULE_LICENSE("GPL");

module_init(ext_support_init);
module_exit(ext_support_exit);
