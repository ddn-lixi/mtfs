/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <linux/module.h>
#include <memory.h>
#include <mtfs_heal.h>
#include <mtfs_lowerfs.h>
#include <mtfs_super.h>
#include <mtfs_dentry.h>
#include <mtfs_inode.h>
#include <mtfs_file.h>
#include <mtfs_junction.h>
#include <mtfs_trace.h>
#include <mtfs_subject.h>
#include <mtfs_record.h>
#include <mtfs_mmap.h>
#include "trace_ext2.h"

struct super_operations trace_ext2_sops =
{
	alloc_inode:    mtfs_alloc_inode,
	destroy_inode:  mtfs_destroy_inode,
	drop_inode:     generic_delete_inode,
	put_super:      mtfs_put_super,
	statfs:         mtfs_statfs,
	clear_inode:    mtfs_clear_inode,
	show_options:   mtfs_show_options,
};

struct inode_operations trace_ext2_symlink_iops =
{
	readlink:       mtfs_readlink,
	follow_link:    mtfs_follow_link,
	put_link:       mtfs_put_link,
	permission:     mtfs_permission,
	setattr:        mtfs_setattr,
	getattr:        mtfs_getattr,
};

struct inode_operations trace_ext2_dir_iops =
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

struct inode_operations trace_ext2_main_iops =
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

struct dentry_operations trace_ext2_dops = {
	d_revalidate:  mtfs_d_revalidate_local,
	d_release:     mtfs_d_release,
};

struct file_operations trace_ext2_dir_fops =
{
	llseek:   mtfs_file_llseek,
	read:     generic_read_dir,
	readdir:  mtfs_readdir,
	ioctl:    mtfs_ioctl,
	open:     mtfs_open,
	release:  mtfs_release,
	/* TODO: fsync, do we really need open? */
};

struct file_operations trace_ext2_main_fops =
{
	llseek:     mtfs_file_llseek,
	read:       mtfs_file_read,
	write:      mtfs_file_write,
#ifdef HAVE_KERNEL_SENDFILE
	sendfile:   mtfs_file_sendfile,
#endif /* HAVE_KERNEL_SENDFILE */
#ifdef HAVE_FILE_READV
	readv:      mtrace_file_readv,
#else /* !HAVE_FILE_READV */
	aio_read:   mtrace_file_aio_read,
#endif /* !HAVE_FILE_READV */
#ifdef HAVE_FILE_WRITEV
	writev:     mtrace_file_writev,
#else /* !HAVE_FILE_WRITEV */
	aio_write:  mtrace_file_aio_write,
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

struct heal_operations trace_ext2_hops =
{
	ho_discard_dentry: heal_discard_dentry_sync,
};

static int trace_ext2_setflags(struct inode *inode, struct file *file, unsigned long arg)
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

static int trace_ext2_getflags(struct inode *inode, struct file *file, unsigned long arg)
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

int trace_ext2_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	MENTRY();

	switch (cmd) {
	case EXT2_IOC_SETFLAGS:
		ret = trace_ext2_setflags(inode, file, arg);
		break;
	case EXT2_IOC_GETFLAGS:
		ret = trace_ext2_getflags(inode, file, arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	MRETURN(ret);
}

struct mtfs_operations trace_ext2_operations = {
	symlink_iops:            &trace_ext2_symlink_iops,
	dir_iops:                &trace_ext2_dir_iops,
	main_iops:               &trace_ext2_main_iops,
	main_fops:               &trace_ext2_main_fops,
	dir_fops:                &trace_ext2_dir_fops,
	sops:                    &trace_ext2_sops,
	dops:                    &trace_ext2_dops,
	aops:                    &mtfs_aops,
	ioctl:                   &trace_ext2_ioctl,
	subject_ops:             &mtrace_subject_ops,
	vm_ops:                  &mtfs_file_vm_ops,
	iupdate_ops:             &mtfs_iupdate_choose,
	io_ops:                  &mtrace_io_ops,
};

const char *supported_secondary_types[] = {
	"ext2",
	NULL,
};

struct mtfs_junction trace_ext2_junction = {
	mj_owner:                THIS_MODULE,
	mj_name:                 "ext2",
	mj_subject:              "TRACE",
	mj_primary_type:         "ext2",
	mj_secondary_types:      supported_secondary_types,
	mj_fs_ops:              &trace_ext2_operations,
};

static int trace_ext2_init(void)
{
	int ret = 0;

	MDEBUG("registering trace_ext2 support\n");

	ret = junction_register(&trace_ext2_junction);
	if (ret) {
		MERROR("failed to register junction: error %d\n", ret);
	}

	return ret;
}

static void trace_ext2_exit(void)
{
	MDEBUG("unregistering trace_ext2 support\n");

	junction_unregister(&trace_ext2_junction);
}

MODULE_AUTHOR("MulTi File System Workgroup");
MODULE_DESCRIPTION("mtfs's support for ext2");
MODULE_LICENSE("GPL");

module_init(trace_ext2_init);
module_exit(trace_ext2_exit);
