/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <linux/module.h>
#include <mtfs_list.h>
#include <mtfs_inode.h>
#include <mtfs_dentry.h>
#include <memory.h>
#include "selfheal_internal.h"

#define DEBUG_SUBSYSTEM S_MTFS

struct mtfs_dentry_list {
	mtfs_list_t list;
	struct dentry *dentry;
};

/*
 * Return d_child needed to be dput() if succeed
 */
struct dentry *_mtfs_dchild_create(struct dentry *dparent, const unsigned char *name,
                                  unsigned int len, umode_t mode, dev_t rdev, int rename)
{
	int ret = 0;
	struct dentry *dchild = NULL;
	HENTRY();

	HASSERT(inode_is_locked(dparent->d_inode));

	dchild = lookup_one_len(name, dparent, len);
	if (IS_ERR(dchild)) {
		HERROR("lookup [%s] under [%*s] failed, ret = %d\n",
		       name, dparent->d_name.len, dparent->d_name.name, ret);
		ret = PTR_ERR(dchild);
		goto out;
	}

	if (dchild->d_inode) {
		if ((dchild->d_inode->i_mode & S_IFMT) == (mode & S_IFMT)) {
			HDEBUG("[%*s/%*s] already existed\n",
			       dparent->d_name.len, dparent->d_name.name, len, name);
			if ((dchild->d_inode->i_mode & S_IALLUGO) != (mode & S_IALLUGO)) {
				HDEBUG("permission mode of [%*s/%*s] is 0%04o, "
				       "which should be 0%04o for security\n",
				       dparent->d_name.len, dparent->d_name.name, len, name,
				       dchild->d_inode->i_mode & S_IALLUGO, S_IRWXU);
			}
			goto out;
		} else {
			HDEBUG("[%*s/%*s] already existed, and is a %s, not a %s\n",
			       dparent->d_name.len, dparent->d_name.name, len, name,
			       mtfs_mode2type(dchild->d_inode->i_mode), mtfs_mode2type(mode));
			if (rename) {
				struct dentry *dchild_new = NULL;
				HDEBUG("Trying to rename [%*s/%*s]\n",
				       dparent->d_name.len, dparent->d_name.name, len, name);
				dchild_new = _mtfs_dchild_add_ino(dparent, dchild);
				if (IS_ERR(dchild_new)) {
					ret = PTR_ERR(dchild_new);
					goto out_put;
				}
				dput(dchild_new);
				ret = -EAGAIN;
				goto out_put;
			} else {
				ret = -EEXIST;
				goto out_put;
			}
		}
	}

	switch (mode & S_IFMT) {
	case S_IFREG:
		ret = vfs_create(dparent->d_inode, dchild, mode, NULL);
		break;
	case S_IFDIR:
		ret = vfs_mkdir(dparent->d_inode, dchild, mode);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		ret = vfs_mknod(dparent->d_inode, dchild, mode, rdev);
		break;
	default:
		HDEBUG("bad file type 0%o\n", mode & S_IFMT);
		ret = -EINVAL;
	}
	if (ret) {
		goto out_put;
	} else {
		HASSERT(dchild->d_inode);
	}
	goto out;
out_put:
	dput(dchild);
out:
	if (ret) {
		dchild = ERR_PTR(ret);
	}
	HRETURN(dchild);
}

/*
 * Return d_child needed to be dput() if succeed
 */
struct dentry *mtfs_dchild_create(struct dentry *dparent, const unsigned char *name,
                                  unsigned int len, umode_t mode, dev_t rdev, int repeat)
{
	struct dentry *dchild = NULL;
	HENTRY();

	HASSERT(dparent);
	HASSERT(name);
	HASSERT(dparent->d_inode);
	mutex_lock(&dparent->d_inode->i_mutex);
	dchild = _mtfs_dchild_create(dparent, name, len, mode, rdev, repeat);
	if (IS_ERR(dchild)) {
		if ((PTR_ERR(dchild) == -EAGAIN) && repeat) {
			dchild = _mtfs_dchild_create(dparent, name, len, mode, rdev, 0);
		}
	}
	mutex_unlock(&dparent->d_inode->i_mutex);
	HRETURN(dchild);
}
EXPORT_SYMBOL(mtfs_dchild_create);

struct dentry *mtfs_dentry_list_mkpath(struct dentry *d_parent, mtfs_list_t *dentry_list)
{
	struct mtfs_dentry_list *tmp_entry = NULL;
	struct dentry *d_child = NULL;
	struct dentry *d_parent_tmp = d_parent;
	HENTRY();

	mtfs_list_for_each_entry(tmp_entry, dentry_list, list) {
		d_child = mtfs_dchild_create(d_parent_tmp, tmp_entry->dentry->d_name.name,
		                             tmp_entry->dentry->d_name.len, S_IFDIR | S_IRWXU, 0, 1);
		if (d_parent_tmp != d_parent) {
			dput(d_parent_tmp);
		}	
		if (IS_ERR(d_child)) {
			HERROR("create [%*s/%*s] failed\n",
			       d_parent_tmp->d_name.len, d_parent_tmp->d_name.name,
			       tmp_entry->dentry->d_name.len, tmp_entry->dentry->d_name.name);
			goto out;
		}
		d_parent_tmp = d_child;
	}
out:
	if (d_child == NULL) {
		d_child = ERR_PTR(-ENOENT);
		HERROR("unexpected: dentry_list is empty\n");
		HBUG();
	}
	HRETURN(d_child);
}

static inline void mtfs_dentry_list_cleanup(mtfs_list_t *dentry_list)
{
	struct mtfs_dentry_list *tmp_entry = NULL;
	struct mtfs_dentry_list *n = NULL;

	mtfs_list_for_each_entry_safe(tmp_entry, n, dentry_list, list) {
		dput(tmp_entry->dentry);
		MTFS_FREE_PTR(tmp_entry);
	}
}

/*
 * needed to dput $dentry after being called.
 */
struct dentry *mtfs_dchild_remove(struct dentry *dparent, const char *name)
{
	int ret = 0;
	struct dentry *dchild = NULL;
	HENTRY();

	HASSERT(dparent);
	HASSERT(name);
	dget(dparent);
	mutex_lock(&dparent->d_inode->i_mutex);

	dchild = lookup_one_len(name, dparent, strlen(name));
	if (IS_ERR(dchild)) {
		HERROR("lookup [%s] under [%*s] failed, ret = %d\n",
		       name, dparent->d_name.len, dparent->d_name.name, ret);
		ret = PTR_ERR(dchild);
		goto out;
	}

	if (dchild->d_inode == NULL) {
		goto out;
	}

	HDEBUG("removing [%*s/%*s]\n",
	       dparent->d_name.len, dparent->d_name.name, dchild->d_name.len, dchild->d_name.name);
	switch (dchild->d_inode->i_mode & S_IFMT) {
	case S_IFDIR:
		ret = vfs_rmdir(dparent->d_inode, dchild);
		break;
	case S_IFREG:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		ret = vfs_unlink(dparent->d_inode, dchild);
		HDEBUG("vfs_unlink, ret = %d\n", ret);
		break;
	default:
		HERROR("bad file type 0%o\n", dchild->d_inode->i_mode & S_IFMT);
		ret = -EINVAL;
	}

	if (ret) {
		dput(dchild);
	} else {
		/* This is not true, but why? */
		//HASSERT(dchild->d_inode == NULL);
	}
	
out:
	mutex_unlock(&dparent->d_inode->i_mutex);
	dput(dparent);
	if (ret) {
		dchild = ERR_PTR(ret);
	}
	HRETURN(dchild);
}
EXPORT_SYMBOL(mtfs_dchild_remove);

/* If succeed, return dchild_new which needed to be dput */
struct dentry *mtfs_dchild_rename2new(struct dentry *dparent, struct dentry *dchild_old,
                                      const unsigned char *name_new, unsigned int len)
{
	struct dentry *dchild_new = NULL;
	int ret = 0;
	HENTRY();

	HASSERT(inode_is_locked(dparent->d_inode));
	dchild_new = lookup_one_len(name_new, dparent, len);
	if (IS_ERR(dchild_new)) {
		HERROR("lookup [%*s/%s] failed, ret = %d\n",
		       dparent->d_name.len, dparent->d_name.name, name_new, ret);
		goto out;
	}

	if (dchild_new->d_inode != NULL) {
		dput(dchild_new);
		dchild_new = ERR_PTR(-EEXIST);
		goto out;
	}

	HDEBUG("renaming [%*s/%*s] to [%*s/%*s]\n",
	       dparent->d_name.len, dparent->d_name.name,
	       dchild_old->d_name.len, dchild_old->d_name.name,
	       dparent->d_name.len, dparent->d_name.name,
	       dchild_new->d_name.len, dchild_new->d_name.name);
	/*
	 * Rename in the same dir, and dir is locked,
	 * no need to lock_rename()
	 */
	ret = vfs_rename(dparent->d_inode, dchild_old,
	                 dparent->d_inode, dchild_new);
	if (ret) {
		dput(dchild_new);
		dchild_new = ERR_PTR(ret);
	}

out:
	HRETURN(dchild_new);
}
EXPORT_SYMBOL(mtfs_dchild_rename2new);

struct dentry *_mtfs_dchild_add_ino(struct dentry *dparent, struct dentry *dchild_old)
{
	char *name_new = NULL;
	struct dentry *dchild_new = NULL;
	HENTRY();

	HASSERT(dparent);
	HASSERT(dparent->d_inode);
	HASSERT(inode_is_locked(dparent->d_inode));
	MTFS_ALLOC(name_new, PATH_MAX);
	if (unlikely(name_new == NULL)) {
		dchild_new = ERR_PTR(-ENOMEM);
		goto out;
	}

	strncpy(name_new, dchild_old->d_name.name, dchild_old->d_name.len);
	sprintf(name_new, "%s:%lx", name_new, dchild_old->d_inode->i_ino);
	dchild_new = mtfs_dchild_rename2new(dparent, dchild_old, name_new, strlen(name_new));

	MTFS_FREE(name_new, PATH_MAX);
out:
	HRETURN(dchild_new);
}
EXPORT_SYMBOL(_mtfs_dchild_add_ino);

struct dentry *mtfs_dchild_add_ino(struct dentry *dparent, const unsigned char *name, unsigned int len)
{
	int ret = 0;
	struct dentry *dchild_old = NULL;
	struct dentry *dchild_new = NULL;
	HENTRY();

	HASSERT(dparent);
	HASSERT(dparent->d_inode);
	HASSERT(name);
	dget(dparent);
	mutex_lock(&dparent->d_inode->i_mutex);

	dchild_old = lookup_one_len(name, dparent, len);
	if (IS_ERR(dchild_old)) {
		HERROR("lookup [%*s/%s] failed, ret = %d\n",
		       dparent->d_name.len, dparent->d_name.name, name, ret);
		ret = PTR_ERR(dchild_old);
		goto out_unlock;
	}

	if (dchild_old->d_inode == NULL) {
		dchild_new = dchild_old;
		goto out_unlock;
	}

	dchild_new = _mtfs_dchild_add_ino(dparent, dchild_old);

	dput(dchild_old);
out_unlock:
	mutex_unlock(&dparent->d_inode->i_mutex);
	dput(dparent);

	if (ret) {
		dchild_new = ERR_PTR(ret);
	}
	HRETURN(dchild_new);
}
EXPORT_SYMBOL(mtfs_dchild_add_ino);

static inline int mutex_lock_if_needed(struct mutex *lock)
{
	if(!mutex_is_locked(lock)) {
		mutex_is_locked(lock);
	}
	return 0;
}

/*
 * p1 and p2 should be directories on the same fs.
 */
struct dentry *mtfs_lock_rename(struct dentry *p1, struct dentry *p2)
{
	struct dentry *p;

	if (p1 == p2) {
		mutex_lock_if_needed(&p1->d_inode->i_mutex);
		return NULL;
	}

	mutex_lock(&p1->d_inode->i_sb->s_vfs_rename_mutex);

	for (p = p1; p->d_parent != p; p = p->d_parent) {
		if (p->d_parent == p2) {
			mutex_lock(&p2->d_inode->i_mutex);
			mutex_lock(&p1->d_inode->i_mutex);
			return p;
		}
	}

	for (p = p2; p->d_parent != p; p = p->d_parent) {
		if (p->d_parent == p1) {
			mutex_lock(&p1->d_inode->i_mutex);
			mutex_lock(&p2->d_inode->i_mutex);
			return p;
		}
	}

	mutex_lock_if_needed(&p1->d_inode->i_mutex);
	mutex_lock_if_needed(&p2->d_inode->i_mutex);
	return NULL;
}

void mtfs_unlock_rename(struct dentry *p1, struct dentry *p2, int p1_locked, int p2_locked)
{
	if (!p1_locked) {
		mutex_unlock(&p1->d_inode->i_mutex);
	}
	if (p1 != p2) {
		if (!p2_locked) {
			mutex_unlock(&p2->d_inode->i_mutex);
		}
		mutex_unlock(&p1->d_inode->i_sb->s_vfs_rename_mutex);
	}
}

int mtfs_backup_branch(struct dentry *dentry, mtfs_bindex_t bindex)
{
	struct dentry *hidden_d_old = mtfs_d2branch(dentry, bindex);
	struct dentry *hidden_d_tmp = NULL;
	struct dentry *hidden_d_recover = NULL;
	struct dentry *hidden_d_root = NULL;
	struct dentry *hidden_d_new = NULL;
	struct mtfs_dentry_list *tmp_entry = NULL;
	MTFS_LIST_HEAD(dentry_list);
	struct mtfs_dentry_list *n = NULL;
	int ret = 0;
	struct dentry *hidden_d_parent_new = NULL;
	struct dentry *hidden_d_parent_old = NULL;
	HENTRY();

	HASSERT(hidden_d_old);

	dget(hidden_d_old);
	hidden_d_recover = mtfs_d_recover_branch(dentry, bindex);
	if (hidden_d_recover == NULL) {
		HERROR("failed to get d_recover for branch[%d]\n", bindex);
		ret = -ENOENT;
		goto out;
	}

	hidden_d_root = mtfs_d_root_branch(dentry, bindex);
	if (hidden_d_root == NULL) {
		HERROR("failed to get d_root for branch[%d]\n", bindex);
		ret = -ENOENT;
		goto out;
	}

	/* TODO: handle long path */
	for(hidden_d_tmp = dget_parent(hidden_d_old); ; hidden_d_tmp = dget_parent(hidden_d_tmp)) {
		if (hidden_d_tmp == hidden_d_root) {
			dput(hidden_d_tmp);
			break;
		}

		if (hidden_d_tmp == hidden_d_recover) {
			HERROR("[%*s] is already under recover directory, skipping\n",
			       dentry->d_name.len, dentry->d_name.name);
			dput(hidden_d_tmp);
			goto out_free_list;
		}

		if (hidden_d_tmp == hidden_d_tmp->d_parent) {
			HERROR("back to the d_root [%*s] of branch[%d]\n",
			       hidden_d_tmp->d_name.len, hidden_d_tmp->d_name.name, bindex);
			dput(hidden_d_tmp);
			HBUG();
			goto out_free_list;
		}

		MTFS_ALLOC_PTR(tmp_entry);
		if (tmp_entry == NULL) {
			ret = -ENOMEM;
			dput(hidden_d_tmp);
			HBUG();
			goto out_free_list;
		}
		tmp_entry->dentry = hidden_d_tmp;
		mtfs_list_add(&tmp_entry->list, &dentry_list);
	}

	if (mtfs_list_empty(&dentry_list)) {
		hidden_d_parent_new = dget(hidden_d_root);
	} else {
		hidden_d_parent_new = mtfs_dentry_list_mkpath(hidden_d_recover, &dentry_list);
		if (IS_ERR(hidden_d_parent_new)) {
			HBUG();
			goto out_free_list;
		}
	}

	hidden_d_new = mtfs_dchild_add_ino(hidden_d_parent_new,
	                                   hidden_d_old->d_name.name, hidden_d_old->d_name.len);
	if (IS_ERR(hidden_d_new)) {
		goto out_dput;		
	}

	hidden_d_parent_old = dget(hidden_d_old->d_parent);
	lock_rename(hidden_d_parent_old, hidden_d_parent_new);
	ret = vfs_rename(hidden_d_parent_old->d_inode, hidden_d_old,
	                 hidden_d_parent_new->d_inode, hidden_d_new);
	unlock_rename(hidden_d_parent_old, hidden_d_parent_new);
	dput(hidden_d_parent_old);
	dput(hidden_d_new);

out_dput:
	dput(hidden_d_parent_new);
out_free_list:
	mtfs_list_for_each_entry_safe(tmp_entry, n, &dentry_list, list) {
		HDEBUG("branch[%d]: dentry [%*s]\n", bindex,
		       tmp_entry->dentry->d_name.len, tmp_entry->dentry->d_name.name);
	}
	mtfs_dentry_list_cleanup(&dentry_list);
out:
	dput(hidden_d_old);
	HRETURN(ret);
}

struct dentry *mtfs_cleanup_branch(struct inode *dir, struct dentry *dentry, mtfs_bindex_t bindex)
{
	int ret = 0;
	struct dentry *hidden_dentry = mtfs_d2branch(dentry, bindex);
	struct dentry *hidden_dir_dentry = mtfs_d2branch(dentry->d_parent, bindex);
	HENTRY();

	HASSERT(inode_is_locked(dir));

	if (hidden_dentry != NULL) {
		ret = mtfs_backup_branch(dentry, bindex);
		if (ret) {
			HERROR("failed to backup branch[%d] of dentry [%*s], ret = %d\n",
			       bindex, dentry->d_name.len, dentry->d_name.name, ret);
			hidden_dentry = ERR_PTR(ret);
			goto out;
		}

		dput(hidden_dentry);
	}

	mtfs_d2branch(dentry, bindex) = NULL;

	mutex_lock(&hidden_dir_dentry->d_inode->i_mutex);
	hidden_dentry = lookup_one_len(dentry->d_name.name, hidden_dir_dentry, dentry->d_name.len);
	mutex_unlock(&hidden_dir_dentry->d_inode->i_mutex);
	if (IS_ERR(hidden_dentry)) {
		goto out;
	} else {
		mtfs_d2branch(dentry, bindex) = hidden_dentry;
	}
out:
	HRETURN(hidden_dentry);
}

int mtfs_lookup_discard_dentry(struct inode *dir, struct dentry *dentry, struct mtfs_operation_list *list)
{
	int ret = 0;
	mtfs_bindex_t bindex = 0;
	mtfs_bindex_t i = 0;
	struct dentry *hidden_dentry = NULL;
	HENTRY();

	HASSERT(inode_is_locked(dir));
	for (i = list->latest_bnum; i < list->bnum; i++) {
		if (list->op_binfo[i].is_suceessful) {
			bindex = list->op_binfo[i].bindex;
			hidden_dentry = mtfs_cleanup_branch(dir, dentry, bindex);
			if (IS_ERR(hidden_dentry)) {
				ret = PTR_ERR(hidden_dentry);
				HERROR("failed to cleanup branch[%d] of dentry [%*s], ret = %d\n",
				       bindex, dentry->d_name.len, dentry->d_name.name, ret);
				goto out;
			}
			
		}
	}
out:
	HRETURN(ret);
}
EXPORT_SYMBOL(mtfs_lookup_discard_dentry);

static int selfheal_max_nthreads = 0;
module_param(selfheal_max_nthreads, int, 0644);
MODULE_PARM_DESC(selfheal_max_nthreads, "Max selfheal thread count to be started.");
EXPORT_SYMBOL(selfheal_max_nthreads);
static struct selfheal_daemon *selfheal_daemons;

int selfheal_daemon_start(int index, int max, const char *name, struct selfheal_daemon_ctl *sc)
{
	HENTRY();
	HRETURN(0);
}

void selfheal_daemon_stop(struct selfheal_daemon_ctl *sc, int force)
{
	HENTRY();
	_HRETURN();
}

void selfheal_daemon_fini(void)
{
	int i = 0;
	HENTRY();

	if (selfheal_daemons == NULL) {
		HERROR("selfheal daemons is not inited yet\n");
		HBUG();
		goto out;
	}

	for (i = 0; i < selfheal_daemons->sd_nthreads; i++) {
		selfheal_daemon_stop(&selfheal_daemons->sd_threads[i], 0);
	}
	MTFS_FREE(selfheal_daemons, selfheal_daemons->sd_size);
	selfheal_daemons = NULL;

out:
	_HRETURN();
}

#define MAX_SELFHEAL_NAME_LENGTH 16
int selfheal_daemon_init(void)
{
	int nthreads = num_online_cpus();
	char name[MAX_SELFHEAL_NAME_LENGTH];
	int size = 0;
	int ret = 0;
	int i = 0;
	HENTRY();

	if (selfheal_max_nthreads > 0 && selfheal_max_nthreads < nthreads) {
                nthreads = selfheal_max_nthreads;
	}

	size = offsetof(struct selfheal_daemon, sd_threads[nthreads]);
	MTFS_ALLOC(selfheal_daemons, size);
	if (selfheal_daemons == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < nthreads; i++) {
		snprintf(name, MAX_SELFHEAL_NAME_LENGTH - 1, "selfheal_%d", i);
		ret = selfheal_daemon_start(i, nthreads, name,
		                            &selfheal_daemons->sd_threads[i]);
		if (ret) {
			i--;
			goto out_stop;
		}
	}

	selfheal_daemons->sd_size = size;
	selfheal_daemons->sd_index = 0;
	selfheal_daemons->sd_nthreads = nthreads;
	goto out;
out_stop:
	for (; i >= 0; i--) {
		selfheal_daemon_stop(&selfheal_daemons->sd_threads[i], 0);
	}
	MTFS_FREE(selfheal_daemons, size);
	selfheal_daemons = NULL;
out:
	HRETURN(ret);
}