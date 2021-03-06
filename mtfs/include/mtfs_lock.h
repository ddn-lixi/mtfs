/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#ifndef __MTFS_LOCK_H__
#define __MTFS_LOCK_H__

#include <spinlock.h>
#include <mtfs_list.h>
#include <mtfs_interval_tree.h>

/* lock modes */
typedef enum {
	MLOCK_MODE_MIN   = 0,
	MLOCK_MODE_READ  = 1,
	MLOCK_MODE_WRITE = 2,
	MLOCK_MODE_NULL  = 4,

	/* For async replication */
	MLOCK_MODE_DIRTY = 8,
	MLOCK_MODE_CLEAN = 16,
	MLOCK_MODE_CHECK = 32,
	MLOCK_MODE_FLUSH = 64,

	MLOCK_MODE_MAX
} mlock_mode_t;

#define MLOCK_MODE_NUM    7

/* lock types */
typedef enum {
	MLOCK_TYPE_MIN    = 0,
	MLOCK_TYPE_PLAIN  = 1,
	MLOCK_TYPE_EXTENT = 2,
	MLOCK_TYPE_MAX
} mlock_type_t;

#define MLOCK_COMPAT_WRITE (MLOCK_MODE_NULL)
#define MLOCK_COMPAT_READ  (MLOCK_MODE_NULL | MLOCK_MODE_READ)
#define MLOCK_COMPAT_NULL  (MLOCK_MODE_NULL | MLOCK_MODE_READ | MLOCK_MODE_WRITE)

/* For async replication */
#define MLOCK_COMPAT_DIRTY (MLOCK_MODE_NULL | MLOCK_MODE_DIRTY)
#define MLOCK_COMPAT_CLEAN (MLOCK_MODE_NULL | MLOCK_MODE_CLEAN)
#define MLOCK_COMPAT_CHECK (MLOCK_MODE_NULL | MLOCK_MODE_CHECK)
#define MLOCK_COMPAT_FLUSH (MLOCK_MODE_NULL)

extern mlock_mode_t mlock_compat_array[];

static inline int mlock_mode_compat(mlock_mode_t exist_mode, mlock_mode_t new_mode)
{
	return (mlock_compat_array[exist_mode] & new_mode);
}

struct mlock;
struct mlock_resource;
struct mlock_extent {
	__u64 start;
	__u64 end;
	__u64 gid;
};

#define MLOCK_EXTENT_EOF 0xffffffffffffffffULL

union mlock_policy_data {
	struct mlock_extent       mlp_extent;
};

/* Return failure when conflict */
#define MLOCK_FL_BLOCK_NOWAIT 0x000001

struct mlock_enqueue_info {
	mlock_mode_t mode;
	union mlock_policy_data data;
	int flag;
};

struct mlock_type_object {
	mlock_type_t mto_type;
	int  (* mto_init)(struct mlock *lock, struct mlock_enqueue_info *einfo);
	void (* mto_grant)(struct mlock *lock);
	int  (* mto_conflict)(mtfs_list_t *queue, struct mlock *lock);
	void (* mto_unlink)(struct mlock *lock);
	void (* mto_free)(struct mlock *lock);
};

struct mlock_interval_tree {
	int                         mlit_size; /* Number of granted locks */
	mlock_mode_t                mlit_mode; /* Lock mode */
	struct mtfs_interval_node  *mlit_root; /* Actual tree */
};

struct mlock_resource {
	mtfs_list_t                mlr_granted;           /* Queue of granted locks */
	mtfs_list_t                mlr_waiting;           /* Queue of waiting locks */
	mtfs_spinlock_t            mlr_lock;              /* Lock, protect mlr_reprocess_linkage */
	int                        mlr_inited;            /* Resource is inited */
	struct mlock_type_object  *mlr_type;              /* Resource type */
	mtfs_list_t                mlr_reprocess_linkage; /* Linkage to reprocess list, protected by mlr_lock */
	/* fields of extent lock */
	struct mlock_interval_tree mlr_itree[MLOCK_MODE_NUM];  /* Interval trees */
};

static inline void mlock_resource_lock(struct mlock_resource *resource)
{
	mtfs_spin_lock(&resource->mlr_lock);
}

static inline void mlock_resource_unlock(struct mlock_resource *resource)
{
	mtfs_spin_unlock(&resource->mlr_lock);
}

static inline int mlock_resource_is_locked(struct mlock_resource *resource)
{
	return mtfs_spin_is_locked(&resource->mlr_lock);
}

/* lock state */
typedef enum {
	MLOCK_STATE_NEW      = 0,
	MLOCK_STATE_WAITING  = 1,
	MLOCK_STATE_GRANTED  = 2,
} mlock_state_t;

struct mlock {
	struct mlock_resource    *ml_resource;    /* Resource to protect */
	mlock_mode_t              ml_mode;        /* Lock mode */
	struct mlock_type_object *ml_type;        /* Lock type */
	mtfs_list_t               ml_res_link;    /* Linkage to resource's lock queues */
	mlock_state_t             ml_state;       /* Lock state */
#if defined (__linux__) && defined(__KERNEL__)
	wait_queue_head_t         ml_waitq;       /* Process waiting for the lock */
#else
	pthread_mutex_t           ml_mutex;
	pthread_cond_t            ml_cond;
#endif
	pid_t                     ml_pid;         /* Pid which created this lock */

	union mlock_policy_data   ml_policy_data; /* Policy data */
	/* fields of plain lock */
	mtfs_list_t               ml_mode_link;   /* Linkage to mode group */

	/* fields of extent lock */
	mtfs_list_t               ml_policy_link; /* Linkage to policy group */
	struct mtfs_interval     *ml_tree_node;   /* Interval tree node */
	
};

static inline int mlock_is_granted(struct mlock *lock)
{
	int ret = 0;

	mlock_resource_lock(lock->ml_resource);
	ret = (lock->ml_state == MLOCK_STATE_GRANTED);
	mlock_resource_unlock(lock->ml_resource);

	return ret;
}

extern void mlock_cancel(struct mlock *lock);
extern struct mlock *mlock_enqueue(struct mlock_resource *resource,
                                   struct mlock_enqueue_info *einfo);
void mlock_resource_init(struct mlock_resource *resource);

extern int mlock_state(struct mlock *lock);

#if defined(__linux__) && defined(__KERNEL__)
#include "mtfs_service.h"

struct mlock_reprocess {
	struct mtfs_service *mls_service;             /* Service of  */
	mtfs_list_t          mls_reprocess_resources; /* Protected by mls_lock */
	mtfs_spinlock_t      mls_lock;                /* Protect mls_reprocess_resources */
};

extern int mlock_init(void);
extern void mlock_fini(void);
#endif /* defined(__linux__) && defined(__KERNEL__) */
#endif /* __MTFS_LOCK_H__ */
