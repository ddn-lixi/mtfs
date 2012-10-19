/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#ifndef __MTFS_RECORD_H__
#define __MTFS_RECORD_H__

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/fs.h>

struct mrecord_head {
	__u32 mrh_len;
	__u32 mrh_type;
};

struct mrecord {
	struct mrecord_head mr_head;
};

struct mrecord_file_info {
	char            *mrfi_fname;
	struct file     *mrfi_filp;
	struct vfsmount *mrfi_mnt;
	struct dentry   *mrfi_dparent;
	struct dentry   *mrfi_dchild;
};

struct mrecord_handle {
	int                        mrh_type;
	const char                *mrh_name;
	struct mrecord_operations *mrh_ops;
	union {
		struct mrecord_file_info mrh_file;
	} u;
};

struct mrecord_operations {
	int (*mro_init)(struct mrecord_handle *handle);
	int (*mro_add)(struct mrecord_handle *handle, struct mrecord *record);
	int (*mro_cleanup)(struct mrecord_handle *handle);
	int (*mro_fini)(struct mrecord_handle *handle);
};

extern struct mrecord_operations mrecord_file_ops;
extern struct mrecord_operations mrecord_nop_ops;

extern int mrecord_init(struct mrecord_handle *handle);
extern int mrecord_fini(struct mrecord_handle *handle);
extern int mrecord_cleanup(struct mrecord_handle *handle);
extern int mrecord_add(struct mrecord_handle *handle, struct mrecord *record);

#else /* !defined (__linux__) && defined(__KERNEL__) */
#error This head is only for kernel space use
#endif /* !defined (__linux__) && defined(__KERNEL__) */

#endif /* __MTFS_RECORD_H__ */
