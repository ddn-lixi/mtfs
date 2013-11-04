/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <debug.h>
#include <memory.h>
#include "async_extent_internal.h"
#include "async_bucket_internal.h"

struct masync_extent *masync_extent_init(struct masync_bucket *bucket)
{
	struct masync_extent *async_extent = NULL;
	MENTRY();

	MTFS_SLAB_ALLOC_PTR(async_extent, mtfs_async_extent_cache);
	if (async_extent == NULL) {
		MERROR("failed to create interval, not enough memory\n");
		goto out;
	}

	async_extent->mae_bucket = bucket;
	async_extent->mae_info = bucket->mab_info;
	mtfs_spin_lock_init(&async_extent->mae_lock);
	MTFS_INIT_LIST_HEAD(&async_extent->mae_lru_linkage);
	MTFS_INIT_LIST_HEAD(&async_extent->mae_cancel_linkage);
	atomic_set(&async_extent->mae_reference, 0);

out:
	MRETURN(async_extent);
}


static void masync_extent_fini(struct masync_extent *async_extent)
{
	MENTRY();

	MASSERT(async_extent->mae_bucket == NULL);
	MASSERT(mtfs_list_empty(&async_extent->mae_lru_linkage));
	MASSERT(mtfs_list_empty(&async_extent->mae_cancel_linkage));
	MASSERT(atomic_read(&async_extent->mae_reference) == 0);
	MTFS_SLAB_FREE_PTR(async_extent, mtfs_async_extent_cache);

	_MRETURN();
}

void masync_extent_get(struct masync_extent *async_extent)
{
	MENTRY();

	atomic_inc(&async_extent->mae_reference);

	_MRETURN();
}

void masync_extent_put(struct masync_extent *async_extent)
{
	MENTRY();

	if (atomic_dec_and_test(&async_extent->mae_reference)) {
		masync_extent_fini(async_extent);
	}

	_MRETURN();
}

/*
 * Flush extent and release it.
 * Please make sure reference is 1 or 2 while calling.
 * Return 0 if succeeded, return 1 if retry is needed.
 */
int masync_extent_flush(struct masync_extent *async_extent,
			char *buf,
                        int buf_size)
{
	struct masync_bucket *bucket = NULL;
	struct mtfs_interval *node = NULL;
	int ret = 0;
	MENTRY();

	node = &async_extent->mae_interval;
	mtfs_spin_lock(&async_extent->mae_lock);
	bucket = async_extent->mae_bucket;
	if (bucket == NULL) {
		/* Already been removed from tree */
		mtfs_spin_unlock(&async_extent->mae_lock);
		goto out;
	}

	if (down_trylock(&bucket->mab_lock)) {
		/* 
		 * Some one else is flushing the file,
		 * retry next time
		 */
		mtfs_spin_unlock(&async_extent->mae_lock);
		ret = 1;
		goto out;
	}

	MASSERT(async_extent->mae_bucket);
	MASSERT(atomic_read(&async_extent->mae_reference) == 2);

	if (bucket->mab_fvalid) {
		ret = masync_sync_file(bucket,
		                       &node->mi_node.in_extent,
		                       buf, buf_size);
		//HASSERT(!ret);
	}
	bucket->mab_number--;
	mtfs_interval_erase(&node->mi_node, &bucket->mab_root);

	/* Export that this extent is not used since now */
	async_extent->mae_bucket = NULL;
	up(&bucket->mab_lock);
	mtfs_spin_unlock(&async_extent->mae_lock);

	/* Extent tree release reference */
	masync_extent_put(async_extent);

out:
	MRETURN(ret);
}