/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <mtfs_io.h>
#include <mtfs_file.h>
#include <mtfs_record.h>
#include <mtfs_trace.h>
#include <mtfs_proc.h>
#include <mtfs_subject.h>
#include <mtfs_super.h>
#include <mtfs_mmap.h>

#define TOPS_READ         0x00000001
#define TOPS_WRITE        0x00000002
#define TOPS_DEFAULT      (TOPS_READ | TOPS_WRITE)

unsigned int mtrace_operations = TOPS_DEFAULT;
module_param(mtrace_operations, int, 0644);

#define TUINT_TIME        0x00000001
#define TUINT_UID         0x00000002
#define TUINT_DEFAULT     (TOPS_TIME | TOPS_UID)

unsigned int mtrace_units = TOPS_DEFAULT;
module_param(mtrace_units, int, 0644);

#ifdef HAVE_SYSCTL_UNNUMBERED
#define CTL_TRACE_MTFS        CTL_UNNUMBERED
#define MTFS_TRACE_OPS_MASK   CTL_UNNUMBERED
#define MTFS_TRACE_UNIT_MASK   CTL_UNNUMBERED
#else /* !define(HAVE_SYSCTL_UNNUMBERED) */
#define CTL_TRACE_MTFS        (0x100)
enum {
	MTFS_TRACE_OPS_MASK = 1, /* Operation */
	MTFS_TRACE_UNIT_MASK,
};
#endif /* !define(HAVE_SYSCTL_UNNUMBERED) */

const char *
mtrace_ops2str(int mask)
{
	switch (1 << mask) {
	default:
		return NULL;
	case TOPS_READ:
		return "read";
	case TOPS_WRITE:
		return "write";
        }
}

const char *
mtrace_unit2str(int mask)
{
	switch (1 << mask) {
	default:
		return NULL;
	case TUINT_TIME:
		return "time";
	case TUINT_UID:
		return "uid";
        }
}

const char *
mtrace_subsys2str(int subsys)
{
        switch (1 << subsys) {
        default:
                return NULL;
        case S_UNDEFINED:
                return "undefined";
        case S_MTFS:
                return "mtfs";
        }
}

static int __mtrace_proc_dobitmasks(void *data, int write,
                                  loff_t pos, void *buffer, int nob)
{
	const int     tmpstrlen = 512;
	char         *tmpstr;
	int           ret;
	unsigned int *mask = data;
	int           is_ops = (mask == &mtrace_operations) ? 1 : 0;
	int           is_unit = (mask == &mtrace_units) ? 1 : 0;
	const char *(*mask2str)(int bit) = NULL;
	MENTRY();

	MTFS_ALLOC(tmpstr, tmpstrlen);
	if (tmpstr == NULL) {
		MERROR("not enough memory\n");
		ret = -ENOMEM;
		goto out;
	}

	MASSERT(is_ops || is_unit);

	if (is_ops) {
		mask2str = mtrace_ops2str;
	} else {
		mask2str = mtrace_unit2str;
	}

	ret = mtfs_common_proc_dobitmasks(data, write,
	                                  pos, buffer, nob,
	                                  0, tmpstr, tmpstrlen,
	                                  mask2str);
	MTFS_FREE(tmpstr, tmpstrlen);
out:
	MRETURN(ret);
}
MTFS_DECLARE_PROC_HANDLER(mtrace_proc_dobitmasks)

static struct ctl_table mtrace_table[] = {
        {
		/* Mask kernel trace */
		.ctl_name = MTFS_TRACE_OPS_MASK,
		.procname = "operations",
		.data     = &mtrace_operations,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &mtrace_proc_dobitmasks,
	},
        {
		/* Mask kernel trace */
		.ctl_name = MTFS_TRACE_UNIT_MASK,
		.procname = "units",
		.data     = &mtrace_units,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &mtrace_proc_dobitmasks,
	},
	{0}
};

struct ctl_table mtrace_top_table[] = {
	{
		.ctl_name = CTL_TRACE_MTFS,
		.procname = "mtfs_trace",
		.mode     = 0555,
		.data     = NULL,
		.maxlen   = 0,
		.child    = mtrace_table,
	},
	{
		.ctl_name = 0
	}
};

#define mtrace_trace_type(type) (mtrace_operations & type)

static void mtrace_io_iter_start_rw(struct mtfs_io *io)
{
	struct mtfs_io_trace *io_trace = &io->subject.mi_trace;
	struct mtfs_io_rw *io_rw = &io->u.mi_rw;
	struct mrecord_head *head = &io_trace->head;
	struct msubject_trace_info *info = NULL;
	MENTRY();

	info = (struct msubject_trace_info *)mtfs_f2subinfo(io_rw->file);
	if (io->mi_bindex != io->mi_bnum - 1) {
		if ((io->mi_type == MIOT_WRITEV && mtrace_trace_type(TOPS_WRITE)) || 
		    (io->mi_type == MIOT_READV && mtrace_trace_type(TOPS_READ))) {
			do_gettimeofday(&io_trace->start);
		}
		mio_iter_start_rw_nonoplist(io);
		if ((io->mi_type == MIOT_WRITEV && mtrace_trace_type(TOPS_WRITE)) || 
		    (io->mi_type == MIOT_READV && mtrace_trace_type(TOPS_READ))) {
			do_gettimeofday(&io_trace->end);
		}
	} else {
		/* Trace branch */
		if ((io->mi_type == MIOT_WRITEV && mtrace_trace_type(TOPS_WRITE)) || 
		    (io->mi_type == MIOT_READV && mtrace_trace_type(TOPS_READ))) {
			head->mrh_len = sizeof(*io_trace);
			io_trace->type = io->mi_type;
			io_trace->result = io->mi_result;
			mrecord_add(&info->msti_handle, head);
		}
	}
	_MRETURN();
}

static void mtrace_io_iter_fini_rw(struct mtfs_io *io, int init_ret)
{
	MENTRY();

	if (unlikely(init_ret)) {
		if (io->mi_bindex == io->mi_bnum - 1) {
			io->mi_break = 1;
		} else {
			io->mi_bindex++;
		}
		goto out;
	}

	if (io->mi_bindex == io->mi_bnum - 1) {
	    	io->mi_break = 1;
	} else {
		io->mi_bindex++;
	}

out:
	_MRETURN();
}

const struct mtfs_io_operations mtrace_io_ops[] = {
	[MIOT_CREATE] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_create,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_LINK] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_link,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_UNLINK] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_unlink,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_MKDIR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_mkdir,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_RMDIR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_rmdir,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_MKNOD] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_mknod,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_RENAME] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist_rename,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_rename,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_SYMLINK] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_symlink,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_READLINK] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_readlink,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_PERMISSION] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_permission,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_READV] = {
		.mio_init       = NULL,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mtrace_io_iter_start_rw,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mtrace_io_iter_fini_rw,
	},
	[MIOT_WRITEV] = {
		.mio_init       = NULL,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mtrace_io_iter_start_rw,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mtrace_io_iter_fini_rw,
	},
	[MIOT_GETATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_getattr,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_SETATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_setattr,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_GETXATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_getxattr,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_SETXATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_setxattr,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_REMOVEXATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_removexattr,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_LISTXATTR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_listxattr,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_READDIR] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_readdir,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_OPEN] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist_noupdate,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_open,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_IOCTL_WRITE] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_ioctl,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_IOCTL_READ] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_ioctl,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
	[MIOT_WRITEPAGE] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = mio_fini_oplist,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_writepage,
		.mio_iter_end   = mio_iter_end_oplist,
		.mio_iter_fini  = mio_iter_fini_write_ops,
	},
	[MIOT_READPAGE] = {
		.mio_init       = mio_init_oplist_flag,
		.mio_fini       = NULL,
		.mio_lock       = NULL,
		.mio_unlock     = NULL,
		.mio_iter_init  = NULL,
		.mio_iter_start = mio_iter_start_readpage,
		.mio_iter_end   = NULL,
		.mio_iter_fini  = mio_iter_fini_read_ops,
	},
};
EXPORT_SYMBOL(mtrace_io_ops);

static int mtrace_io_init_rw(struct mtfs_io *io, int is_write,
                             struct file *file, const struct iovec *iov,
                             unsigned long nr_segs, loff_t *ppos, size_t rw_size)
{
	int ret = 0;
	mtfs_io_type_t type;
	struct mtfs_io_rw *io_rw = &io->u.mi_rw;
	MENTRY();

	type = is_write ? MIOT_WRITEV : MIOT_READV;
	io->mi_ops = &mtrace_io_ops[type];
	io->mi_type = type;
	io->mi_oplist_dentry = file->f_dentry;
	io->mi_bindex = 0;
	io->mi_bnum = mtfs_f2bnum(file);
	io->mi_break = 0;
	io->mi_oplist_dentry = file->f_dentry;

	io_rw->file = file;
	io_rw->iov = iov;
	io_rw->nr_segs = nr_segs;
	io_rw->ppos = ppos;
	io_rw->iov_length = sizeof(*iov) * nr_segs;
	io_rw->rw_size = rw_size;

	MRETURN(ret);
}

static ssize_t mtrace_file_rw(int is_write, struct file *file, const struct iovec *iov,
                              unsigned long nr_segs, loff_t *ppos)
{
	ssize_t size = 0;
	int ret = 0;
	struct mtfs_io *io = NULL;
	size_t rw_size = 0;
	MENTRY();

	rw_size = get_iov_count(iov, &nr_segs);
	if (rw_size <= 0) {
		ret = rw_size;
		goto out;
	}

	MTFS_SLAB_ALLOC_PTR(io, mtfs_io_cache);
	if (io == NULL) {
		MERROR("not enough memory\n");
		size = -ENOMEM;
		goto out;
	}

	ret = mtrace_io_init_rw(io, is_write, file, iov, nr_segs, ppos, rw_size);
	if (ret) {
		size = ret;
		goto out_free_io;
	}

	ret = mtfs_io_loop(io);
	if (ret) {
		MERROR("failed to loop on io\n");
		size = ret;
	} else {
		size = io->mi_result.ssize;
	}

out_free_io:
	MTFS_SLAB_FREE_PTR(io, mtfs_io_cache);
out:
	MRETURN(size);
}

#ifdef HAVE_FILE_READV
ssize_t mtrace_file_readv(struct file *file, const struct iovec *iov,
                          unsigned long nr_segs, loff_t *ppos)
{
	ssize_t size = 0;
	MENTRY();

	size = mtrace_file_rw(READ, file, iov, nr_segs, ppos);

	MRETURN(size);
}
EXPORT_SYMBOL(mtrace_file_readv);

ssize_t mtrace_file_read(struct file *file, char __user *buf, size_t len,
                         loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf,
	                           .iov_len = len };
	return mtrace_file_readv(file, &local_iov, 1, ppos);
}
EXPORT_SYMBOL(mtrace_file_read);

ssize_t mtrace_file_writev(struct file *file, const struct iovec *iov,
                        unsigned long nr_segs, loff_t *ppos)
{
	ssize_t size = 0;
	MENTRY();

	size = mtrace_file_rw(WRITE, file, iov, nr_segs, ppos);

	MRETURN(size);
}
EXPORT_SYMBOL(mtrace_file_writev);
#else /* !HAVE_FILE_READV */
ssize_t mtrace_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
                             unsigned long nr_segs, loff_t pos)
{
	ssize_t size = 0;
	struct file *file = iocb->ki_filp;
	MENTRY();

	size = mtrace_file_rw(READ, file, iov, nr_segs, &pos);
	if (size > 0) {
		iocb->ki_pos = pos + size;
	}

	MRETURN(size);
}
EXPORT_SYMBOL(mtrace_file_aio_read);

ssize_t mtrace_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
                              unsigned long nr_segs, loff_t pos)
{
	ssize_t size = 0;
	struct file *file = iocb->ki_filp;
	MENTRY();

	size = mtrace_file_rw(WRITE, file, iov, nr_segs, &pos);
	if (size > 0) {
		iocb->ki_pos = pos + size;
	}

	MRETURN(size);
}
EXPORT_SYMBOL(mtrace_file_aio_write);

#endif /* !HAVE_FILE_READV */

int mtrace_super_init(struct super_block *sb)
{
	int ret = 0;
	struct mrecord_handle *handle = NULL;
	struct msubject_trace_info *info = NULL;
	struct mrecord_file_info *mrh_file = NULL;
	mtfs_bindex_t bindex = mtfs_s2bnum(sb) - 1;
	MENTRY();

	MTFS_ALLOC_PTR(info);
	if (info == NULL) {
		ret = -ENOMEM;
		MERROR("not enough memory\n");
		goto out;
	}
	handle = &info->msti_handle;
	handle->mrh_ops = &mrecord_file_ops;

	mrh_file = &handle->u.mrh_file;
	mrh_file->mrfi_mnt = mtfs_s2mntbranch(sb, bindex);
	mrh_file->mrfi_dparent = mtfs_s2bdreserve(sb, bindex);
	mrh_file->mrfi_fname = MTFS_RESERVE_RECORD;
	ret = mrecord_init(handle);
	if (ret) {
		MERROR("failed to init handle, ret = %d\n", ret);
		goto out_free_handle;
	}

	ret = mrecord_cleanup(handle);
	if (ret) {
		MERROR("failed to cleanup handle, ret = %d\n", ret);
		goto out_fini_handle;
	}

#ifdef CONFIG_SYSCTL
#ifdef HAVE_REGISTER_SYSCTL_2ARGS
	info->msti_ctl_table = register_sysctl_table(mtrace_top_table, 0);
#else /* !HAVE_REGISTER_SYSCTL_2ARGS */
	info->msti_ctl_table = register_sysctl_table(mtrace_top_table);
#endif /* !HAVE_REGISTER_SYSCTL_2ARGS */
#endif /* CONFIG_SYSCTL */

	mtfs_s2subinfo(sb) = (void *)info;
	goto out;
out_fini_handle:
	mrecord_fini(handle);
out_free_handle:
	MTFS_FREE_PTR(handle);
out:
	MRETURN(ret);
}

int mtrace_super_fini(struct super_block *sb)
{
	int ret = 0;
	struct msubject_trace_info *info = NULL;
	MENTRY();

	info = (struct msubject_trace_info *)mtfs_s2subinfo(sb);
	unregister_sysctl_table(info->msti_ctl_table);
	mrecord_fini(&info->msti_handle);
	MTFS_FREE_PTR(info);
	MRETURN(ret);
}

struct mtfs_subject_operations mtrace_subject_ops = {
	mso_super_init:                 mtrace_super_init,
	mso_super_fini:                 mtrace_super_fini,
};
EXPORT_SYMBOL(mtrace_subject_ops);
