/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <linux/module.h>
#include <mtfs_heal.h>
#include <mtfs_lowerfs.h>
#include <mtfs_super.h>
#include <mtfs_dentry.h>
#include <mtfs_inode.h>
#include <mtfs_file.h>
#include <mtfs_mmap.h>
#include <mtfs_junction.h>
#include "replica_ext.h"

struct super_operations mtfs_ext_sops =
{
	alloc_inode:    mtfs_alloc_inode,
	destroy_inode:  mtfs_destroy_inode,
	drop_inode:     generic_delete_inode,
	put_super:      mtfs_put_super,
	statfs:         mtfs_statfs,
	clear_inode:    mtfs_clear_inode,
	show_options:   mtfs_show_options,
};

struct inode_operations mtfs_ext_symlink_iops =
{
	readlink:       mtfs_readlink,
	follow_link:    mtfs_follow_link,
	put_link:       mtfs_put_link,
	permission:     mtfs_permission,
	setattr:        mtfs_setattr,
	getattr:        mtfs_getattr,
};

struct inode_operations mtfs_ext_dir_iops =
{
	create:	        mtfs_create,
	lookup:	        mtfs_lookup_nonnd,
	link:           mtfs_link,
	unlink:	        mtfs_unlink,
	symlink:        mtfs_symlink,
	mkdir:	        mtfs_mkdir,
	rmdir:	        mtfs_rmdir,
	mknod:	        mtfs_mknod,
	rename:	        mtfs_rename,
	permission:     mtfs_permission,
	setattr:        mtfs_setattr,
	getattr:        mtfs_getattr,
	setxattr:       mtfs_setxattr,
	getxattr:       mtfs_getxattr,
	removexattr:    mtfs_removexattr,
	listxattr:      mtfs_listxattr,
};

struct inode_operations mtfs_ext_main_iops =
{
	permission:     mtfs_permission,
	setattr:        mtfs_setattr,
	getattr:        mtfs_getattr,
	setxattr:       mtfs_setxattr,
	getxattr:       mtfs_getxattr,
	removexattr:    mtfs_removexattr,
	listxattr:      mtfs_listxattr,
};

int mtfs_d_revalidate_local(struct dentry *dentry, struct nameidata *nd)
{
	int ret = 0;
	MENTRY();

	MDEBUG("d_revalidate [%.*s]\n", dentry->d_name.len, dentry->d_name.name);

	if (dentry->d_flags & DCACHE_MTFS_INVALID) {
		ret = 0;
		goto out;
	}
	ret = 1;
out:
	MRETURN(ret);
}

struct dentry_operations mtfs_ext_dops = {
	d_revalidate:  mtfs_d_revalidate_local,
	d_release:     mtfs_d_release,
};

struct file_operations mtfs_ext_dir_fops =
{
	llseek:   mtfs_file_llseek,
	read:     generic_read_dir,
	readdir:  mtfs_readdir,
	ioctl:    mtfs_ioctl,
	open:     mtfs_open,
	release:  mtfs_release,
	/* TODO: fsync, do we really need open? */
};

struct file_operations mtfs_ext_main_fops =
{
	llseek:     mtfs_file_llseek,
	read:       mtfs_file_read,
	write:      mtfs_file_write,
#ifdef HAVE_KERNEL_SENDFILE
	sendfile:   mtfs_file_sendfile,
#endif /* HAVE_KERNEL_SENDFILE */
#ifdef HAVE_FILE_READV
	readv:      mtfs_file_readv,
#else /* !HAVE_FILE_READV */
	aio_read:   mtfs_file_aio_read,
#endif /* !HAVE_FILE_READV */
#ifdef HAVE_FILE_WRITEV
	writev:     mtfs_file_writev,
#else /* !HAVE_FILE_WRITEV */
	aio_write:  mtfs_file_aio_write,
#endif /* !HAVE_FILE_WRITEV */
	readdir:    mtfs_readdir,
	poll:       mtfs_poll,
	ioctl:      mtfs_ioctl,
	mmap:       mtfs_file_mmap,
	open:       mtfs_open,
	release:    mtfs_release,
	fsync:      mtfs_fsync,
	/* TODO: splice_read, splice_write */
};

struct heal_operations mtfs_ext_hops =
{
	ho_discard_dentry: heal_discard_dentry_sync,
};

static int mtfs_ext2_setflags(struct inode *inode, struct file *file, unsigned long arg)
{
	int ret = 0;
	int flags = 0;
	MENTRY();

	ret = mtfs_ioctl_write(inode, file, EXT2_IOC_SETFLAGS, arg, 0);
	if (ret < 0) {
		goto out;
	}

	ret = get_user(flags, (int *)arg);
	if (ret) {
		MERROR("failed to get_user, ret = %d\n", ret);
		goto out;
	}

	inode->i_flags = ext2_to_inode_flags(flags | EXT2_RESERVED_FL);
out:
	MRETURN(ret);
}

static int mtfs_ext2_getflags(struct inode *inode, struct file *file, unsigned long arg)
{
	int ret = 0;
	int flags = 0;
	MENTRY();

	ret = mtfs_ioctl_read(inode, file, EXT2_IOC_GETFLAGS, (unsigned long)&flags, 1);
	if (ret < 0) {
		goto out;
	}
	ret = put_user(flags, (int __user *)arg);
out:
	MRETURN(ret);
}

int mtfs_ext2_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	MENTRY();

	switch (cmd) {
	case EXT2_IOC_SETFLAGS:
		ret = mtfs_ext2_setflags(inode, file, arg);
		break;
	case EXT2_IOC_GETFLAGS:
		ret = mtfs_ext2_getflags(inode, file, arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	MRETURN(ret);
}

struct mtfs_operations mtfs_ext2_operations = {
	symlink_iops:            &mtfs_ext_symlink_iops,
	dir_iops:                &mtfs_ext_dir_iops,
	main_iops:               &mtfs_ext_main_iops,
	main_fops:               &mtfs_ext_main_fops,
	dir_fops:                &mtfs_ext_dir_fops,
	sops:                    &mtfs_ext_sops,
	dops:                    &mtfs_ext_dops,
	aops:                    &mtfs_aops,
	vm_ops:                  &mtfs_file_vm_ops,
	ioctl:                   &mtfs_ext2_ioctl,
	subject_ops:              NULL,
	iupdate_ops:             &mtfs_iupdate_choose,
	io_ops:                  &mtfs_io_ops,
};

struct mtfs_operations mtfs_ext3_operations = {
	symlink_iops:            &mtfs_ext_symlink_iops,
	dir_iops:                &mtfs_ext_dir_iops,
	main_iops:               &mtfs_ext_main_iops,
	main_fops:               &mtfs_ext_main_fops,
	dir_fops:                &mtfs_ext_dir_fops,
	sops:                    &mtfs_ext_sops,
	dops:                    &mtfs_ext_dops,
	aops:                    &mtfs_aops,
	vm_ops:                  &mtfs_file_vm_ops,
	ioctl:                    NULL,
	subject_ops:              NULL,
	iupdate_ops:             &mtfs_iupdate_choose,
	io_ops:                  &mtfs_io_ops,
};

struct mtfs_operations mtfs_ext4_operations = {
	symlink_iops:            &mtfs_ext_symlink_iops,
	dir_iops:                &mtfs_ext_dir_iops,
	main_iops:               &mtfs_ext_main_iops,
	main_fops:               &mtfs_ext_main_fops,
	dir_fops:                &mtfs_ext_dir_fops,
	sops:                    &mtfs_ext_sops,
	dops:                    &mtfs_ext_dops,
	aops:                    &mtfs_aops,
	vm_ops:                  &mtfs_file_vm_ops,
	ioctl:                    NULL,
	subject_ops:              NULL,
	iupdate_ops:             &mtfs_iupdate_choose,
	io_ops:                  &mtfs_io_ops,
};

const char *ext2_supported_secondary_types[] = {
	"ext2",
	NULL,
};

const char *ext3_supported_secondary_types[] = {
	"ext3",
	NULL,
};

const char *ext4_supported_secondary_types[] = {
	"ext4",
	NULL,
};

struct mtfs_junction mtfs_ext2_junction = {
	mj_owner:                THIS_MODULE,
	mj_name:                 "ext2",
	mj_subject:              "SYNC_REPLICA",
	mj_primary_type:         "ext2",
	mj_secondary_types:      ext2_supported_secondary_types,
	mj_fs_ops:              &mtfs_ext2_operations,
};

struct mtfs_junction mtfs_ext3_junction = {
	mj_owner:                THIS_MODULE,
	mj_name:                 "ext3",
	mj_subject:              "SYNC_REPLICA",
	mj_primary_type:         "ext3",
	mj_secondary_types:      ext3_supported_secondary_types,
	mj_fs_ops:              &mtfs_ext3_operations,
};

struct mtfs_junction mtfs_ext4_junction = {
	mj_owner:                THIS_MODULE,
	mj_name:                 "ext4",
	mj_subject:              "SYNC_REPLICA",
	mj_primary_type:         "ext4",
	mj_secondary_types:      ext4_supported_secondary_types,
	mj_fs_ops:              &mtfs_ext4_operations,
};

static int ext_support_init(void)
{
	int ret = 0;

	MDEBUG("registering mtfs async juntion for ext2\n");

	ret = junction_register(&mtfs_ext2_junction);
	if (ret) {
		MERROR("failed to register junction for ext2, ret = %d\n", ret);
		goto out;
	}

	ret = junction_register(&mtfs_ext3_junction);
	if (ret) {
		MERROR("failed to register junction for ext3, ret = %d\n", ret);
		goto out_unregister_ext2;
	}

	ret = junction_register(&mtfs_ext4_junction);
	if (ret) {
		MERROR("failed to register junction for ext4, ret = %d\n", ret);
		goto out_unregister_ext3;
	}
	goto out;

out_unregister_ext3:
	junction_unregister(&mtfs_ext3_junction);	
out_unregister_ext2:
	junction_unregister(&mtfs_ext2_junction);
out:
	return ret;
}

static void ext_support_exit(void)
{
	MDEBUG("unregistering mtfs sync juntion for ext2/ext3/ext4\n");

	junction_unregister(&mtfs_ext4_junction);
	junction_unregister(&mtfs_ext3_junction);
	junction_unregister(&mtfs_ext2_junction);
}

MODULE_AUTHOR("MulTi File System Workgroup");
MODULE_DESCRIPTION("mtfs's sync replica junction for ext2");
MODULE_LICENSE("GPL");

module_init(ext_support_init);
module_exit(ext_support_exit);
