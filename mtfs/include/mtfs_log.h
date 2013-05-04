/*
 * Copied from Lustre-2.x
 */

#ifndef __MTFS_LOG_H__
#define __MTFS_LOG_H__

#if defined (__linux__) && defined(__KERNEL__)

/** Identifier for a single log object */
struct mlog_logid {
        __u64                   mgl_oid;
        __u64                   mgl_ogr;
        __u32                   mgl_ogen;
} __attribute__((packed));

/** Log record header - stored in little endian order.
 * Each record must start with this struct, end with a mlog_rec_tail,
 * and be a multiple of 256 bits in size.
 */
struct mlog_rec_hdr {
        __u32                   mrh_len;
        __u32                   mrh_index;
        __u32                   mrh_type;
        __u32                   padding;
};

struct mlog_rec_tail {
        __u32 mrt_len;
        __u32 mrt_index;
};

struct mlog_gen {
        __u64 mnt_cnt;
        __u64 conn_cnt;
} __attribute__((packed));

/* On-disk header structure of each log object, stored in little endian order */
#define MLOG_CHUNK_SIZE         8192
#define MLOG_HEADER_SIZE        (96)
#define MLOG_BITMAP_BYTES       (MLOG_CHUNK_SIZE - MLOG_HEADER_SIZE)

#define MLOG_MIN_REC_SIZE       (24) /* round(mlog_rec_hdr + mlog_rec_tail) */

/* flags for the logs */
#define MLOG_F_ZAP_WHEN_EMPTY   0x1
#define MLOG_F_IS_CAT           0x2
#define MLOG_F_IS_PLAIN         0x4

struct mlog_log_hdr {
	struct mlog_rec_hdr     mlh_hdr;
	__u64                   mlh_timestamp;
	__u32                   mlh_count;
	__u32                   mlh_bitmap_offset;
	__u32                   mlh_size;
	__u32                   mlh_flags;
	__u32                   mlh_cat_idx;
	__u32                   mlh_reserved[MLOG_HEADER_SIZE/sizeof(__u32) - 23];
	__u32                   mlh_bitmap[MLOG_BITMAP_BYTES/sizeof(__u32)];
	struct mlog_rec_tail    mlh_tail;
} __attribute__((packed));

#define MLOG_BITMAP_SIZE(mlh)  ((mlh->mlh_hdr.mrh_len -         \
                                 mlh->mlh_bitmap_offset -       \
                                 sizeof(mlh->mlh_tail)) * 8)

struct mlog_ctxt {
        struct dentry           *moc_dlog; /* Which directory the logs saved in */
        struct vfsmount         *moc_mnt;  /* Mnt of lowerfs */
        struct mtfs_lowerfs     *moc_lowerfs;

        struct mlog_operations  *moc_logops;
};

/** log cookies are used to reference a specific log file and a record therein */
struct mlog_cookie {
	struct mlog_logid       mgc_lgl;
	__u32                   mgc_subsys;
	__u32                   mgc_index;
	__u32                   mgc_padding;
} __attribute__((packed));

struct mlog_handle;

struct plain_handle_data {
        struct list_head    phd_entry;
        struct mlog_handle *phd_cat_handle;
        struct mlog_cookie  phd_cookie; /* cookie of this log in its cat */
        int                 phd_last_idx;
};

struct cat_handle_data {
        struct list_head        chd_head;
        struct mlog_handle     *chd_current_log; /* currently open log */
};

/* In-memory descriptor for a log object or log catalog */
struct mlog_handle {
	struct rw_semaphore     mgh_lock;
	struct mlog_logid       mgh_id;              /* id of this log */
	struct mlog_log_hdr    *mgh_hdr;
	struct file            *mgh_file;
	int                     mgh_last_idx;
	int                     mgh_cur_idx;    /* used during mlog_process */
	__u64                   mgh_cur_offset; /* used during mlog_process */
	struct mlog_ctxt       *mgh_ctxt;
	union {
		struct plain_handle_data phd;
		struct cat_handle_data   chd;
	} u;
};

/* Log data record types - there is no specific reason that these need to
 * be related to the RPC opcodes, but no reason not to (may be handy later?)
 */
#define MLOG_OP_MAGIC 0x10600000
#define MLOG_OP_MASK  0xfff00000

typedef enum {
        MLOG_PAD_MAGIC   = MLOG_OP_MAGIC | 0x00000,
	MLOG_GEN_REC     = MLOG_OP_MAGIC | 0x40000,
} mlog_op_type;

struct mlog_operations {
	int (*mop_write_rec)(struct mlog_handle *loghandle,
	                     struct mlog_rec_hdr *rec,
	                     struct mlog_cookie *logcookies, int numcookies,
	                     void *, int idx);
	int (*mop_destroy)(struct mlog_handle *handle);
	int (*mop_next_block)(struct mlog_handle *h, int *curr_idx,
	                      int next_idx, __u64 *offset, void *buf, int len);
	int (*mop_prev_block)(struct mlog_handle *h,
	                      int prev_idx, void *buf, int len);
	int (*mop_create)(struct mlog_ctxt *ctxt, struct mlog_handle **,
	                  struct mlog_logid *logid, const char *name);
	int (*mop_close)(struct mlog_handle *handle);
	int (*mop_read_header)(struct mlog_handle *handle);

	int (*mop_setup)(int ctxt_idx,
	                 int count,
	                 struct mlog_logid *logid);
	int (*mop_sync)(struct mlog_ctxt *ctxt);
	int (*mop_cleanup)(struct mlog_ctxt *ctxt);
	int (*mop_add)(struct mlog_ctxt *ctxt, struct mlog_rec_hdr *rec,
	               struct mlog_cookie *logcookies, int numcookies);
	int (*mop_cancel)(struct mlog_ctxt *ctxt,
	                  int count, struct mlog_cookie *cookies, int flags);
};

extern struct mlog_handle *mlog_alloc_handle(void);
extern void mlog_free_handle(struct mlog_handle *loghandle);
extern int mlog_run_tests(struct mlog_ctxt *ctxt);
extern struct mlog_operations mlog_vfs_operations;

static inline int mlog_create(struct mlog_ctxt *ctxt,
                              struct mlog_handle **res,
                              struct mlog_logid *logid,
                              const char *name)
{
	struct mlog_operations *mop = NULL;
	int ret = 0;
	MENTRY();

	mop = ctxt->moc_logops;
	MASSERT(mop->mop_create);

	ret = mop->mop_create(ctxt, res, logid, name);

	MRETURN(ret);
}

static inline int mlog_close(struct mlog_handle *loghandle)
{
	struct mlog_ctxt *ctxt = NULL;
	struct mlog_operations *mop = NULL;
	int ret = 0;
	MENTRY();

	ctxt = loghandle->mgh_ctxt;
	mop = ctxt->moc_logops;
	MASSERT(mop->mop_close);

	ret = mop->mop_close(loghandle);
	mlog_free_handle(loghandle);
	MRETURN(ret);
}

static inline struct mlog_ctxt *mlog_context_init(struct dentry *log_directory,
                                                  struct vfsmount *mnt,
                                                  struct mtfs_lowerfs *lowerfs,
                                                  struct mlog_operations *logops)
{
	struct mlog_ctxt *ctxt = NULL;
	MENTRY();

	MTFS_ALLOC_PTR(ctxt);
	if (ctxt == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	ctxt->moc_dlog = log_directory;
	ctxt->moc_mnt = mnt;
	ctxt->moc_lowerfs = lowerfs;
	ctxt->moc_logops = logops;
out:
	MRETURN(ctxt);
}

static inline void mlog_context_fini(struct mlog_ctxt *ctxt)
{
	MTFS_FREE_PTR(ctxt);
}

#else /* !defined (__linux__) && defined(__KERNEL__) */
#error This head is only for kernel space use
#endif /* !defined (__linux__) && defined(__KERNEL__) */


#endif /* __MTFS_LOG_H__ */
