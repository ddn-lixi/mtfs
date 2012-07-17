/*
 * Copied from Lustre-2.2
 */

#include <linux/mutex.h>
#include <linux/sched.h>
#include <debug.h>
#include <memory.h>
#include <mtfs_list.h>
#include <thread.h>
#include "thread_internal.h"
#include "service_internal.h"

static inline int
mtfs_threads_increasable(struct mtfs_service *svc)
{
	return svc->srv_threads_running +
		svc->srv_threads_starting < svc->srv_threads_max;
}

static inline void thread_add_flags(struct mtfs_thread *thread, __u32 flags)
{
	thread->t_flags |= flags;
}

static int mtfs_service_main(void *arg)
{
	int ret = 0;
	HENTRY();

	HRETURN(ret);
}

#define MTFS_MAX_THREAD_NAME_LENGTH 32
int mtfs_start_thread(struct mtfs_service *svc)
{
	struct l_wait_info lwi = { 0 };
	struct mtfs_thread *thread = NULL;
	int ret = 0;
	char name[MTFS_MAX_THREAD_NAME_LENGTH];
	struct mtfs_svc_data d;
	HENTRY();

	if (unlikely(svc->srv_is_stopping)) {
		HERROR("this service is stopping\n");
		ret = -ESRCH;
		goto out;
	}

	if (!mtfs_threads_increasable(svc)) {
		HERROR("this service can not increase threads\n");
		ret = -EMFILE;
		goto out;
	}

	MTFS_ALLOC_PTR(thread);
	if (thread == NULL) {
		HERROR("not enough memory\n");
		ret = -ENOMEM;
		goto out;
	}

	init_waitqueue_head(&thread->t_ctl_waitq);
	
	spin_lock(&svc->srv_lock);
	{
		if (!mtfs_threads_increasable(svc)) {
			spin_unlock(&svc->srv_lock);
			HERROR("this service can not increase threads\n");
			ret = -EMFILE;
			goto out_free_thread;
		}
		svc->srv_threads_starting++;
		thread->t_id = svc->srv_threads_next_id++;
		thread_add_flags(thread, SVC_STARTING);
		thread->t_svc = svc;

		mtfs_list_add(&thread->t_link, &svc->srv_threads);
	}
	spin_unlock(&svc->srv_lock);

	sprintf(name, "%s_%02d", svc->srv_thread_name, thread->t_id);
	d.svc = svc;
	d.name = name;
	d.thread = thread;

	HDEBUG("starting thread '%s'\n", name);
	ret = mtfs_create_thread(mtfs_service_main, &d, 0);
	if (ret < 0) {
		HERROR("cannot start thread '%s', ret = %d\n", name, ret);
		goto out_delete_thread;
	}

	l_wait_event(thread->t_ctl_waitq,
	             thread_is_running(thread) || thread_is_stopped(thread),
	             &lwi);

	ret = thread_is_stopped(thread) ? thread->t_id : 0;
	goto out;

out_delete_thread:
	spin_lock(&svc->srv_lock);
	mtfs_list_del(&thread->t_link);
	--svc->srv_threads_starting;
	spin_unlock(&svc->srv_lock);
out_free_thread:
	MTFS_FREE_PTR(thread);
out:
	HRETURN(ret);
}