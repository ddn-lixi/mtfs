/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

/*
 * For both kernel and userspace use
 * DO NOT use anything special that opposes this purpose
 */

#if defined (__linux__) && defined(__KERNEL__)
#include <linux/module.h>
#include <linux/sched.h>
#include <thread.h>
#include "main_internal.h"
#endif /* defined (__linux__) && defined(__KERNEL__) */

#include <compat.h>
#include <debug.h>
#include <memory.h>
#include <mtfs_lock.h>
#include <mtfs_list.h>
#include <mtfs_interval_tree.h>

/* compatibility matrix */
mlock_mode_t mlock_compat_array[] = {
        [MLOCK_MODE_READ]  MLOCK_COMPAT_READ,
        [MLOCK_MODE_WRITE] MLOCK_COMPAT_WRITE,
        [MLOCK_MODE_NULL]  MLOCK_COMPAT_NULL,
        [MLOCK_MODE_DIRTY] MLOCK_COMPAT_DIRTY,
        [MLOCK_MODE_CLEAN] MLOCK_COMPAT_CLEAN,
        [MLOCK_MODE_CHECK] MLOCK_COMPAT_CHECK,
        [MLOCK_MODE_FLUSH] MLOCK_COMPAT_FLUSH,
};

struct mlock_plain_position {
	mtfs_list_t *res_link;
	mtfs_list_t *mode_link;
};

static void mlock_plain_search_prev(mtfs_list_t *queue,
                                    struct mlock *req_lock,
                                    struct mlock_plain_position *prev)
{
	mtfs_list_t *tmp = NULL;
	struct mlock *tmp_lock = NULL;
	struct mlock *lock_mode_end = NULL;
	MENTRY();

	mtfs_list_for_each(tmp, queue) {
		tmp_lock = mtfs_list_entry(tmp, struct mlock, ml_res_link);
		lock_mode_end = mtfs_list_entry(tmp_lock->ml_mode_link.prev,
                                                struct mlock, ml_mode_link);

		if (tmp_lock->ml_mode != req_lock->ml_mode) {
			/* jump to last lock of mode group */
			tmp = &lock_mode_end->ml_res_link;
			continue;
		}

		/* Suitable mode group is found */

		/* Insert point is last lock of the mode group */
		prev->res_link = &lock_mode_end->ml_res_link;
		prev->mode_link = &lock_mode_end->ml_mode_link;
		break;
	}

	/*
	 * Insert point is last lock on the queue,
         * new mode group and new policy group are started
         */
	prev->res_link = queue->prev;
	prev->mode_link = &req_lock->ml_mode_link;

	_MRETURN();
}

static void mlock_plain_unlink(struct mlock *lock)
{
	MENTRY();

	MASSERT(mlock_resource_is_locked(lock->ml_resource));
	mtfs_list_del_init(&lock->ml_res_link);
	mtfs_list_del_init(&lock->ml_mode_link);
	_MRETURN();
}

static void mlock_plain_add(mtfs_list_t *queue, struct mlock *lock)
{
	struct mlock_plain_position prev;
	MENTRY();

	MASSERT(mlock_resource_is_locked(lock->ml_resource));

	mlock_plain_search_prev(queue, lock, &prev);
        mtfs_list_add(&lock->ml_res_link, prev.res_link);
        mtfs_list_add(&lock->ml_mode_link, prev.mode_link);	
	_MRETURN();
}

/*
 * 0 if the lock is compatible
 * 1 if the lock is not compatible
 */
static int mlock_plain_conflict(mtfs_list_t *queue, struct mlock *req_lock)
{
	int ret = 0;
	struct mlock_resource *resource = req_lock->ml_resource;
	struct mlock *old_lock = NULL;
	mtfs_list_t *tmp = NULL;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(queue == &resource->mlr_granted || queue == &resource->mlr_waiting);

	mtfs_list_for_each(tmp, queue) {
		old_lock = mtfs_list_entry(tmp, struct mlock, ml_res_link);

		if (req_lock == old_lock) {
			MASSERT(queue == &resource->mlr_waiting);
			break;
		}

		if (mlock_mode_compat(old_lock->ml_mode, req_lock->ml_mode)) {
			if (queue == &resource->mlr_granted) {
				/* last lock in mode group */
				tmp = &mtfs_list_entry(old_lock->ml_mode_link.prev,
		                                       struct mlock,
		                                       ml_mode_link)->ml_res_link;
			}
			continue;
		}

		ret = 1;
		break;
	}

	MRETURN(ret);
}

static int mlock_plain_init(struct mlock *lock, struct mlock_enqueue_info *einfo)
{
	int ret = 0;
	MENTRY();

	MTFS_INIT_LIST_HEAD(&lock->ml_res_link);
	MTFS_INIT_LIST_HEAD(&lock->ml_mode_link);

	MRETURN(ret);
}

static void mlock_plain_free(struct mlock *lock)
{
	MENTRY();

	MTFS_SLAB_FREE_PTR(lock, mtfs_lock_cache);
	_MRETURN();
}

static void mlock_plain_grant(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(lock->ml_state == MLOCK_STATE_NEW || lock->ml_state == MLOCK_STATE_WAITING);

	if (lock->ml_state == MLOCK_STATE_WAITING) {
		mtfs_list_del_init(&lock->ml_res_link);
	}

	mlock_plain_add(&resource->mlr_granted, lock);
	_MRETURN();
}

struct mlock_type_object mlock_type_pain = {
	.mto_type     = MLOCK_TYPE_PLAIN,
	.mto_init     = mlock_plain_init,
	.mto_grant    = mlock_plain_grant,
	.mto_conflict = mlock_plain_conflict,
	.mto_unlink   = mlock_plain_unlink,
	.mto_free     = mlock_plain_free,
};

static inline int __is_po2(unsigned long long val)
{
        return !(val & (val - 1));
}

#define IS_PO2(val) __is_po2((unsigned long long)(val))

static inline int lock_mode_to_index(mlock_mode_t mode)
{
	int index = 0;

	MASSERT(mode != 0);
	MASSERT(IS_PO2(mode));
	for (index = -1; mode; index++, mode >>= 1) ;
	MASSERT(index < MLOCK_MODE_MAX);
	return index;
}

void mlock_interval_attach(struct mtfs_interval *node,
                           struct mlock *lock)
{
	MASSERT(lock->ml_tree_node == NULL);
	MASSERT(lock->ml_type->mto_type == MLOCK_TYPE_EXTENT);
	MENTRY();

	mtfs_list_add_tail(&lock->ml_policy_link, &node->mi_linkage);
	lock->ml_tree_node = node;
	_MRETURN();
}

struct mtfs_interval *mlock_interval_detach(struct mlock *lock)
{
	struct mtfs_interval *node = lock->ml_tree_node;
	MENTRY();

	MASSERT(node);
	MASSERT(!mtfs_list_empty(&node->mi_linkage));
	lock->ml_tree_node = NULL;
	mtfs_list_del_init(&lock->ml_policy_link);

	MRETURN(mtfs_list_empty(&node->mi_linkage) ? node : NULL);
}

static int mlock_extent_init(struct mlock *lock, struct mlock_enqueue_info *einfo)
{
	struct mtfs_interval *node = NULL;
	int ret = 0;
	__u64 start;
 	__u64 end;
	MENTRY();

	MASSERT(lock->ml_type->mto_type == MLOCK_TYPE_EXTENT);
	MTFS_SLAB_ALLOC_PTR(node, mtfs_interval_cache);
	if (node == NULL) {
		MERROR("failed to create interval, not enough memory\n");
		ret = -ENOMEM;
		goto out;
	}

	lock->ml_policy_data.mlp_extent = einfo->data.mlp_extent;
	start = lock->ml_policy_data.mlp_extent.start;
	end = lock->ml_policy_data.mlp_extent.end;
	MASSERT(start <= end);

	MTFS_INIT_LIST_HEAD(&node->mi_linkage);
	mlock_interval_attach(node, lock);

	MTFS_INIT_LIST_HEAD(&lock->ml_res_link);
out:
	MRETURN(ret);
}

static void mlock_extent_free(struct mlock *lock)
{
	MTFS_SLAB_FREE_PTR(lock->ml_tree_node, mtfs_interval_cache);
	MTFS_SLAB_FREE_PTR(lock, mtfs_lock_cache);
}

static void mlock_interval_free(struct mtfs_interval *node)
{
	MASSERT(mtfs_list_empty(&node->mi_linkage));
	MASSERT(!mtfs_interval_is_intree(&node->mi_node));
	MTFS_SLAB_FREE_PTR(node, mtfs_interval_cache);
}

static void mlock_extent_grant(struct mlock *lock)
{
	struct mtfs_interval_node *found = NULL;
	struct mtfs_interval_node **root = NULL;
	struct mtfs_interval *node = NULL;
	struct mlock_extent *extent = NULL;
	struct mlock_resource *resource = lock->ml_resource;
	int index = 0;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(lock->ml_state == MLOCK_STATE_NEW || lock->ml_state == MLOCK_STATE_WAITING);

	node = lock->ml_tree_node;
	MASSERT(node);
	MASSERT(!mtfs_interval_is_intree(&node->mi_node));

	index = lock_mode_to_index(lock->ml_mode);
	MASSERT(lock->ml_mode == 1 << index);
	MASSERT(lock->ml_mode == resource->mlr_itree[index].mlit_mode);

	extent = &lock->ml_policy_data.mlp_extent;
	mtfs_interval_set(&node->mi_node, extent->start, extent->end);

	root = &resource->mlr_itree[index].mlit_root;
	found = mtfs_interval_insert(&node->mi_node, root);
	if (found) {
		/* When will be here? */
		struct mtfs_interval *tmp = mlock_interval_detach(lock);
		MASSERT(tmp != NULL);
		mlock_interval_free(tmp);
		mlock_interval_attach(mtfs_node2interval(found), lock);
	}
	resource->mlr_itree[index].mlit_size++;

	if (lock->ml_state == MLOCK_STATE_WAITING) {
		mtfs_list_del_init(&lock->ml_res_link);
	}
	mtfs_list_add_tail(&lock->ml_res_link, &resource->mlr_granted);
	_MRETURN();
}

void mlock_extent_unlink(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	struct mtfs_interval *node = lock->ml_tree_node;
	struct mlock_interval_tree *tree = NULL;
	int index = 0;
	MENTRY();

	MASSERT(node);
	MASSERT(mtfs_interval_is_intree(&node->mi_node));

	index = lock_mode_to_index(lock->ml_mode);
	MASSERT(lock->ml_mode == 1 << index);
	tree = &resource->mlr_itree[index];
	MASSERT(tree->mlit_root);
	tree->mlit_size--;
	node = mlock_interval_detach(lock);
	if (node) {
		mtfs_interval_erase(&node->mi_node, &tree->mlit_root);
		mlock_interval_free(node);
	}

	mtfs_list_del_init(&lock->ml_res_link);

	_MRETURN();
}

static int mlock_extent_conflict_granted(struct mlock *lock)
{
	int ret = 0;
	struct mlock_interval_tree *tree = NULL;
	struct mtfs_interval_node_extent extent;
	int index = 0;
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));

	extent.start = lock->ml_policy_data.mlp_extent.start,
	extent.end = lock->ml_policy_data.mlp_extent.end;

	for (index = 0; index < MLOCK_MODE_NUM; index++) {
		tree = &resource->mlr_itree[index];

		if (mlock_mode_compat(tree->mlit_mode, lock->ml_mode)) {
			continue;
		}

		ret = mtfs_interval_is_overlapped(tree->mlit_root, &extent);
		if (ret) {
			MDEBUG("interval [%llu, %llu] overlapped\n",
			       extent.start, extent.end);
			break;
		}
	}

	MRETURN(ret);
}

static int mlock_extent_conflict_waiting(struct mlock *req_lock)
{
	int ret = 0;
	struct mlock_resource *resource = req_lock->ml_resource;
	struct mlock *old_lock = NULL;
	mtfs_list_t *tmp = NULL;
	mtfs_list_t *queue = &resource->mlr_waiting;
	__u64 old_start = 0;
	__u64 old_end = 0;
	__u64 req_start = 0;
	__u64 req_end = 0;
	MENTRY();

	mtfs_list_for_each(tmp, queue) {
		old_lock = mtfs_list_entry(tmp, struct mlock, ml_res_link);

		if (req_lock == old_lock) {
			break;
		}

		if (mlock_mode_compat(old_lock->ml_mode, req_lock->ml_mode)) {
			continue;
		}

		req_start = req_lock->ml_policy_data.mlp_extent.start;
		req_end = req_lock->ml_policy_data.mlp_extent.end;
		old_start = old_lock->ml_policy_data.mlp_extent.start;
		old_end = old_lock->ml_policy_data.mlp_extent.end;
		if (old_end < req_start || old_start > req_end) {
			/* Doesn't overlap skip it */
			continue;
		}
		ret = 1;
		break;
	}

	MRETURN(ret);
}

/*
 * 0 if the lock is compatible
 * 1 if the lock is not compatible
 */
static int mlock_extent_conflict(mtfs_list_t *queue, struct mlock *lock)
{
	int ret = 0;
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(queue == &resource->mlr_granted || queue == &resource->mlr_waiting);
	if (queue == &resource->mlr_granted) {
		ret = mlock_extent_conflict_granted(lock);
	} else {
		ret = mlock_extent_conflict_waiting(lock);
	}

	MRETURN(ret);
}

struct mlock_type_object mlock_type_extent = {
	.mto_type     = MLOCK_TYPE_EXTENT,
	.mto_init     = mlock_extent_init,
	.mto_grant    = mlock_extent_grant,
	.mto_conflict = mlock_extent_conflict,
	.mto_unlink   = mlock_extent_unlink,
	.mto_free     = mlock_extent_free,
};

#if defined (__linux__) && defined(__KERNEL__)
static void mlock_init_waitq(struct mlock *lock)
{
	init_waitqueue_head(&lock->ml_waitq);
}
#else /* !defined (__linux__) && defined(__KERNEL__) */
static void mlock_init_waitq(struct mlock *lock)
{
	pthread_mutex_init(&lock->ml_mutex, NULL);
	pthread_cond_init(&lock->ml_cond, NULL);
}
#endif /* !defined (__linux__) && defined(__KERNEL__) */

static struct mlock *mlock_create(struct mlock_resource *resource, struct mlock_enqueue_info *einfo)
{
	struct mlock *lock = NULL;
	int ret = 0;
	MENTRY();

	MTFS_SLAB_ALLOC_PTR(lock, mtfs_lock_cache);
	if (lock == NULL) {
		MERROR("failed to create lock, not enough memory\n");
		goto out;
	}

	lock->ml_resource = resource;
	lock->ml_mode = einfo->mode;
	lock->ml_type = resource->mlr_type;
	lock->ml_state = MLOCK_STATE_NEW;
	mlock_init_waitq(lock);
#if defined (__linux__) && defined(__KERNEL__)
	lock->ml_pid = current->pid;
#endif
	if (lock->ml_type->mto_init) {
		ret = lock->ml_type->mto_init(lock, einfo);
		if (ret) {
			goto out_free_lock;
		}
	}
	goto out;
out_free_lock:
	MTFS_SLAB_FREE_PTR(lock, mtfs_lock_cache);
	lock = NULL;
out:
	MRETURN(lock);
}

static void mlock_grant(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(lock->ml_state == MLOCK_STATE_NEW || lock->ml_state == MLOCK_STATE_WAITING);

	if (lock->ml_type->mto_grant) {
		lock->ml_type->mto_grant(lock);
	}
	lock->ml_state = MLOCK_STATE_GRANTED;

	_MRETURN();
}

static void mlock_unlink(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	if (lock->ml_type->mto_unlink) {
		lock->ml_type->mto_unlink(lock);
	}
	_MRETURN();
}

/*
 * 0 if the lock is compatible
 * 1 if the lock is not compatible
 */
static int mlock_confilct(mtfs_list_t *queue, struct mlock *lock)
{
	int ret = 0;
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(queue == &resource->mlr_granted || queue == &resource->mlr_waiting);

	if (lock->ml_type->mto_conflict) {
		ret = lock->ml_type->mto_conflict(queue, lock);
	}

	MRETURN(ret);
}

static void mlock_pend(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(mtfs_list_empty(&lock->ml_res_link));
	MASSERT(lock->ml_state == MLOCK_STATE_NEW);
        lock->ml_state = MLOCK_STATE_WAITING;

	mtfs_list_add_tail(&lock->ml_res_link, &resource->mlr_waiting);

	_MRETURN();
}

static int mlock_enqueue_try_nolock(struct mlock *lock, int flag)
{
	struct mlock_resource *resource = lock->ml_resource;
	int ret = 0;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(lock->ml_state == MLOCK_STATE_NEW || lock->ml_state == MLOCK_STATE_WAITING);

	ret = mlock_confilct(&resource->mlr_granted, lock);
	if (ret) {
		MDEBUG("lock %p conflicting with granted queue\n", lock);
		goto out_pend;
	}

	ret = mlock_confilct(&resource->mlr_waiting, lock);
	if (ret) {
		MDEBUG("lock %p conflicting with waiting queue\n", lock);
		goto out_pend;
	}

	mlock_grant(lock);
	goto out;
out_pend:
	if (flag & MLOCK_FL_BLOCK_NOWAIT) {
		ret = -EWOULDBLOCK;
	} else if (lock->ml_state == MLOCK_STATE_NEW) {
		mlock_pend(lock);
	}
out:
	MRETURN(ret);
}

static int mlock_enqueue_try(struct mlock *lock, int flag)
{
	struct mlock_resource *resource = lock->ml_resource;
	int ret = 0;
	MENTRY();

	MASSERT(lock->ml_state == MLOCK_STATE_NEW || lock->ml_state == MLOCK_STATE_WAITING);

	mlock_resource_lock(resource);
	ret = mlock_enqueue_try_nolock(lock, flag);
	mlock_resource_unlock(resource);
	MRETURN(ret);
}

#if defined (__linux__) && defined(__KERNEL__)

#define mlock_wait_condition(lock, condition)  \
    mtfs_wait_condition(lock->ml_waitq, condition)

#else /* !defined (__linux__) && defined(__KERNEL__) */

#define mlock_wait_condition(lock, condition)   \
do {                                            \
    pthread_mutex_lock(&lock->ml_mutex);        \
    {                                           \
        while (condition == 0) {                \
            pthread_cond_wait(&lock->ml_cond,   \
                              &lock->ml_mutex); \
        }    	                                \
    }   	                                \
    pthread_mutex_unlock (&lock->ml_mutex);     \
} while (0)

#endif /* !defined (__linux__) && defined(__KERNEL__) */

static void mlock_free(struct mlock *lock)
{
	MENTRY();

	if (lock->ml_type->mto_free) {
		lock->ml_type->mto_free(lock);
	}
	_MRETURN();
}

struct mlock *mlock_enqueue(struct mlock_resource *resource, struct mlock_enqueue_info *einfo)
{
	int ret = 0;
	struct mlock *lock = NULL;
	MENTRY();

	lock = mlock_create(resource, einfo);
	if (lock == NULL) {
		MERROR("failed to create lock\n");
		ret = -ENOMEM;
		goto out;
	}

	MASSERT(lock->ml_state == MLOCK_STATE_NEW);
	ret = mlock_enqueue_try(lock, einfo->flag);
	if (ret && !(einfo->flag & MLOCK_FL_BLOCK_NOWAIT)) {
		mlock_wait_condition(lock, mlock_is_granted(lock));
		MDEBUG("lock is granted because another lock is canceled\n");
		ret = 0;
	}
out:
	if (ret) {
		if (lock != NULL) {
			mlock_free(lock);
		}
		lock = ERR_PTR(ret);
	}
	MRETURN(lock);
}
EXPORT_SYMBOL(mlock_enqueue);

void mlock_destroy(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	MASSERT(mtfs_list_empty(&lock->ml_res_link));

	MTFS_SLAB_FREE_PTR(lock, mtfs_lock_cache);

	_MRETURN();
}

#if defined (__linux__) && defined(__KERNEL__)
static inline void mlock_wakeup(struct mlock *lock)
{
	wake_up(&lock->ml_waitq);
}
#else /* !defined (__linux__) && defined(__KERNEL__) */
static inline void mlock_wakeup(struct mlock *lock)
{
	pthread_mutex_lock(&lock->ml_mutex);
	{
		pthread_cond_broadcast(&lock->ml_cond);
	}
	pthread_mutex_unlock(&lock->ml_mutex);
}
#endif /* !defined (__linux__) && defined(__KERNEL__) */

void mlock_resource_reprocess(struct mlock_resource *resource)
{
	mtfs_list_t *tmp = NULL;
	mtfs_list_t *pos = NULL;
	struct mlock *lock = NULL;
	int ret = 0;
	MENTRY();

	MASSERT(mlock_resource_is_locked(resource));
	mtfs_list_for_each_safe(tmp, pos, &resource->mlr_waiting) {
		lock = mtfs_list_entry(tmp, struct mlock, ml_res_link);
		MASSERT(lock->ml_state == MLOCK_STATE_WAITING);
		ret = mlock_enqueue_try_nolock(lock, 0);
		if (ret) {
			continue;
		}
		mlock_wakeup(lock);
	}
	_MRETURN();
}

#if defined(__linux__) && defined(__KERNEL__)
static void mlock_resource_add2list(struct mlock_resource *resource);
#endif /* defined(__linux__) && defined(__KERNEL__) */

void mlock_cancel(struct mlock *lock)
{
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	MASSERT(lock->ml_state == MLOCK_STATE_GRANTED);

	mlock_resource_lock(resource);
	mlock_unlink(lock);
#if defined(__linux__) && defined(__KERNEL__)
	mlock_destroy(lock);
	mlock_resource_unlock(resource);
	mlock_resource_add2list(resource);
#else /* !defined(__linux__) && defined(__KERNEL__) */
	mlock_resource_reprocess(resource);
	mlock_destroy(lock);
	mlock_resource_unlock(resource);
#endif /* !defined(__linux__) && defined(__KERNEL__) */

	_MRETURN();
}
EXPORT_SYMBOL(mlock_cancel);

int mlock_state(struct mlock *lock)
{
	int state = 0;
	struct mlock_resource *resource = lock->ml_resource;
	MENTRY();

	mlock_resource_lock(resource);
	state = lock->ml_state;
	mlock_resource_unlock(resource);

	MRETURN(state);
}
EXPORT_SYMBOL(mlock_state);

#define MLOCK_TYPE_DEFAULT (&mlock_type_extent)

void mlock_resource_init(struct mlock_resource *resource)
{
	int index = 0;
	MENTRY();

	MTFS_INIT_LIST_HEAD(&resource->mlr_granted);
	MTFS_INIT_LIST_HEAD(&resource->mlr_waiting);
	MTFS_INIT_LIST_HEAD(&resource->mlr_reprocess_linkage);

	mtfs_spin_lock_init(&resource->mlr_lock);
	resource->mlr_type = MLOCK_TYPE_DEFAULT;

	/* initialize interval trees for each lock mode*/
	for (index = 0; index < MLOCK_MODE_NUM; index++) {
		resource->mlr_itree[index].mlit_size = 0;
		resource->mlr_itree[index].mlit_mode = 1 << index;
		resource->mlr_itree[index].mlit_root = NULL;
	}
	resource->mlr_inited = 1;
	_MRETURN();
}

#if defined(__linux__) && defined(__KERNEL__)
struct mlock_reprocess the_mlock;

static int mlock_service_busy(struct mtfs_service *service, struct mservice_thread *thread)
{
	int ret = 0;
	struct mlock_reprocess *reprocess = (struct mlock_reprocess *)service->srv_data;
	MENTRY();

	mtfs_spin_lock(&reprocess->mls_lock);
	ret = !mtfs_list_empty(&reprocess->mls_reprocess_resources);
	mtfs_spin_unlock(&reprocess->mls_lock);
	MRETURN(ret);
}

static int mlock_service_main(struct mtfs_service *service, struct mservice_thread *thread)
{
	int ret = 0;
	struct mlock_reprocess *reprocess = (struct mlock_reprocess *)service->srv_data;
	struct mlock_resource *resource = NULL;
	MENTRY();

	while(1) {
		if (mservice_wait_event(service, thread)) {
			break;
		}

		mtfs_spin_lock(&reprocess->mls_lock);
		if (mtfs_list_empty(&reprocess->mls_reprocess_resources)) {
			mtfs_spin_unlock(&reprocess->mls_lock);
			continue;
		}
		resource = mtfs_list_entry(reprocess->mls_reprocess_resources.next,
	                               struct mlock_resource,
		                           mlr_reprocess_linkage);
		mtfs_list_del_init(&resource->mlr_reprocess_linkage);
		mtfs_spin_unlock(&reprocess->mls_lock);

		/* Reprocess the resource */
		mlock_resource_lock(resource);
		mlock_resource_reprocess(resource);
		mlock_resource_unlock(resource);
	}
	MRETURN(ret);
}

#define MLOCK_SERVICE_NAME "mtfs_lock"

static int _mlock_init(struct mlock_reprocess *reprocess)
{
	int ret = 0;
	MENTRY();

	MTFS_INIT_LIST_HEAD(&reprocess->mls_reprocess_resources);
	mtfs_spin_lock_init(&reprocess->mls_lock);
	reprocess->mls_service = mservice_init(MLOCK_SERVICE_NAME,
	                                       MLOCK_SERVICE_NAME,
	                                       1, 1, 100,
	                                       0, mlock_service_main,
	                                       mlock_service_busy,
	                                       reprocess);
	if (reprocess->mls_service == NULL) {
		MERROR("failed to init service of lock\n");
		ret = -EINVAL;
	}

	MRETURN(ret);
}

int mlock_init(void)
{
	return _mlock_init(&the_mlock);
}
EXPORT_SYMBOL(mlock_init);

static void _mlock_fini(struct mlock_reprocess *reprocess)
{
	MASSERT(mtfs_list_empty(&reprocess->mls_reprocess_resources));
	mservice_fini(reprocess->mls_service);
}

void mlock_fini(void)
{
	_mlock_fini(&the_mlock);
}
EXPORT_SYMBOL(mlock_fini);

static void mlock_service_wakeup(void)
{
	MENTRY();

	wake_up(&the_mlock.mls_service->srv_waitq);
	_MRETURN();
}

static void mlock_resource_add2list(struct mlock_resource *resource)
{
	struct mlock_reprocess *reprocess = &the_mlock;
	int added = 0;
	MENTRY();

	mtfs_spin_lock(&reprocess->mls_lock);
	if (mtfs_list_empty(&resource->mlr_reprocess_linkage)) {
		mlock_resource_lock(resource);
		if (!mtfs_list_empty(&resource->mlr_waiting)) {
			mtfs_list_add_tail(&resource->mlr_reprocess_linkage,
			                   &reprocess->mls_reprocess_resources);
			added = 1;
		}
		mlock_resource_unlock(resource);
	}
	mtfs_spin_unlock(&reprocess->mls_lock);

	if (added) {
		mlock_service_wakeup();
	}

	_MRETURN();
}
#endif /* defined(__linux__) && defined(__KERNEL__) */