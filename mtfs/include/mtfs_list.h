/*
 * Copied from Lustre-2.1
 */

#ifndef __MTFS_LIST_H__
#define __MTFS_LIST_H__

#if defined (__linux__) && defined(__KERNEL__)

#include <linux/list.h>

typedef struct list_head mtfs_list_t;

#define __mtfs_list_add(new, prev, next)      __list_add(new, prev, next)
#define mtfs_list_add(new, head)              list_add(new, head)

#define mtfs_list_add_tail(new, head)         list_add_tail(new, head)

#define __mtfs_list_del(prev, next)           __list_del(prev, next)
#define mtfs_list_del(entry)                  list_del(entry)
#define mtfs_list_del_init(entry)             list_del_init(entry)

#define mtfs_list_move(list, head)            list_move(list, head)
#define mtfs_list_move_tail(list, head)       list_move_tail(list, head)

#define mtfs_list_empty(head)                 list_empty(head)
#define mtfs_list_empty_careful(head)         list_empty_careful(head)

#define __mtfs_list_splice(list, head)        __list_splice(list, head)
#define mtfs_list_splice(list, head)          list_splice(list, head)

#if 0 /* TODO: HAVE_LIST_SPICE_TAIL */
#define mtfs_list_splice_tail(list, head)     list_splice_tail(list, head)
#else
/* __list_splice as re-implemented on 2.6.27, we backport it */
static inline void __list_splice_new_27(const struct list_head *list,
                                           struct list_head *prev,
                                           struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

static inline void mtfs_list_splice_tail(struct list_head *list,
				         struct list_head *head)
{
	if (!mtfs_list_empty(list))
		__list_splice_new_27(list, head->prev, head);
}
#endif

#define mtfs_list_splice_init(list, head)     list_splice_init(list, head)

#define mtfs_list_entry(ptr, type, member)    list_entry(ptr, type, member)
#define mtfs_list_for_each(pos, head)         list_for_each(pos, head)
#define mtfs_list_for_each_safe(pos, n, head) list_for_each_safe(pos, n, head)
#define mtfs_list_for_each_prev(pos, head)    list_for_each_prev(pos, head)
#define mtfs_list_for_each_entry(pos, head, member) \
        list_for_each_entry(pos, head, member)
#define mtfs_list_for_each_entry_reverse(pos, head, member) \
        list_for_each_entry_reverse(pos, head, member)
#define mtfs_list_for_each_entry_safe(pos, n, head, member) \
        list_for_each_entry_safe(pos, n, head, member)
#ifdef list_for_each_entry_safe_from
#define mtfs_list_for_each_entry_safe_from(pos, n, head, member) \
        list_for_each_entry_safe_from(pos, n, head, member)
#endif /* list_for_each_entry_safe_from */
#define mtfs_list_for_each_entry_continue(pos, head, member) \
        list_for_each_entry_continue(pos, head, member)

#define MTFS_LIST_HEAD_INIT(n)		     LIST_HEAD_INIT(n)
#define MTFS_LIST_HEAD(n)		     LIST_HEAD(n)
#define MTFS_INIT_LIST_HEAD(p)		     INIT_LIST_HEAD(p)

typedef struct hlist_head mtfs_hlist_head_t;
typedef struct hlist_node mtfs_hlist_node_t;

#define mtfs_hlist_unhashed(h)              hlist_unhashed(h)

#define mtfs_hlist_empty(h)                 hlist_empty(h)

#define __mtfs_hlist_del(n)                 __hlist_del(n)
#define mtfs_hlist_del(n)                   hlist_del(n)
#define mtfs_hlist_del_init(n)              hlist_del_init(n)

#define mtfs_hlist_add_head(n, next)        hlist_add_head(n, next)
#define mtfs_hlist_add_before(n, next)      hlist_add_before(n, next)
#define mtfs_hlist_add_after(n, next)       hlist_add_after(n, next)

#define mtfs_hlist_entry(ptr, type, member) hlist_entry(ptr, type, member)
#define mtfs_hlist_for_each(pos, head)      hlist_for_each(pos, head)
#define mtfs_hlist_for_each_safe(pos, n, head) \
        hlist_for_each_safe(pos, n, head)
#define mtfs_hlist_for_each_entry(tpos, pos, head, member) \
        hlist_for_each_entry(tpos, pos, head, member)
#define mtfs_hlist_for_each_entry_continue(tpos, pos, member) \
        hlist_for_each_entry_continue(tpos, pos, member)
#define mtfs_hlist_for_each_entry_from(tpos, pos, member) \
        hlist_for_each_entry_from(tpos, pos, member)
#define mtfs_hlist_for_each_entry_safe(tpos, pos, n, head, member) \
        hlist_for_each_entry_safe(tpos, pos, n, head, member)

#define MTFS_HLIST_HEAD_INIT		   HLIST_HEAD_INIT
#define MTFS_HLIST_HEAD(n)		   HLIST_HEAD(n)
#define MTFS_INIT_HLIST_HEAD(p)		   INIT_HLIST_HEAD(p)
#define MTFS_INIT_HLIST_NODE(p)		   INIT_HLIST_NODE(p)

#else /* !defined (__linux__) || !defined(__KERNEL__) */
#include <stdlib.h> /* For NULL */

/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

#define prefetch(a) ((void)a)

struct mtfs_list_head {
	struct mtfs_list_head *next, *prev;
};

typedef struct mtfs_list_head mtfs_list_t;

#define MTFS_LIST_HEAD_INIT(name) { &(name), &(name) }

#define MTFS_LIST_HEAD(name) \
	mtfs_list_t name = MTFS_LIST_HEAD_INIT(name)

#define MTFS_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/**
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __mtfs_list_add(mtfs_list_t * new,
                                  mtfs_list_t * prev,
                                  mtfs_list_t * next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/**
 * Insert an entry at the start of a list.
 * \param new  new entry to be inserted
 * \param head list to add it to
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void mtfs_list_add(mtfs_list_t *new,
                                mtfs_list_t *head)
{
	__mtfs_list_add(new, head, head->next);
}

/**
 * Insert an entry at the end of a list.
 * \param new  new entry to be inserted
 * \param head list to add it to
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void mtfs_list_add_tail(mtfs_list_t *new,
                                     mtfs_list_t *head)
{
	__mtfs_list_add(new, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __mtfs_list_del(mtfs_list_t *prev,
                                  mtfs_list_t *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * Remove an entry from the list it is currently in.
 * \param entry the entry to remove
 * Note: list_empty(entry) does not return true after this, the entry is in an
 * undefined state.
 */
static inline void mtfs_list_del(mtfs_list_t *entry)
{
	__mtfs_list_del(entry->prev, entry->next);
}

/**
 * Remove an entry from the list it is currently in and reinitialize it.
 * \param entry the entry to remove.
 */
static inline void mtfs_list_del_init(mtfs_list_t *entry)
{
	__mtfs_list_del(entry->prev, entry->next);
	MTFS_INIT_LIST_HEAD(entry);
}

/**
 * Remove an entry from the list it is currently in and insert it at the start
 * of another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void mtfs_list_move(mtfs_list_t *list,
                                 mtfs_list_t *head)
{
	__mtfs_list_del(list->prev, list->next);
	mtfs_list_add(list, head);
}

/**
 * Remove an entry from the list it is currently in and insert it at the end of
 * another list.
 * \param list the entry to move
 * \param head the list to move it to
 */
static inline void mtfs_list_move_tail(mtfs_list_t *list,
                                      mtfs_list_t *head)
{
	__mtfs_list_del(list->prev, list->next);
	mtfs_list_add_tail(list, head);
}

/**
 * Test whether a list is empty
 * \param head the list to test.
 */
static inline int mtfs_list_empty(mtfs_list_t *head)
{
	return head->next == head;
}

/**
 * Test whether a list is empty and not being modified
 * \param head the list to test
 *
 * Tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * NOTE: using mtfs_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is mtfs_list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 */
static inline int mtfs_list_empty_careful(const mtfs_list_t *head)
{
        mtfs_list_t *next = head->next;
        return (next == head) && (next == head->prev);
}

static inline void __mtfs_list_splice(mtfs_list_t *list,
                                     mtfs_list_t *head)
{
	mtfs_list_t *first = list->next;
	mtfs_list_t *last = list->prev;
	mtfs_list_t *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * Join two lists
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is in an
 * undefined state on return.
 */
static inline void mtfs_list_splice(mtfs_list_t *list,
                                   mtfs_list_t *head)
{
	if (!mtfs_list_empty(list))
		__mtfs_list_splice(list, head);
}

/**
 * Join two lists and reinitialise the emptied list.
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is empty
 * on return.
 */
static inline void mtfs_list_splice_init(mtfs_list_t *list,
                                        mtfs_list_t *head)
{
	if (!mtfs_list_empty(list)) {
		__mtfs_list_splice(list, head);
		MTFS_INIT_LIST_HEAD(list);
	}
}

/**
 * Get the container of a list
 * \param ptr	 the embedded list.
 * \param type	 the type of the struct this is embedded in.
 * \param member the member name of the list within the struct.
 */
#define mtfs_list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

/**
 * Iterate over a list
 * \param pos	the iterator
 * \param head	the list to iterate over
 *
 * Behaviour is undefined if \a pos is removed from the list in the body of the
 * loop.
 */
#define mtfs_list_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
		pos = pos->next, prefetch(pos->next))

/**
 * Iterate over a list safely
 * \param pos	the iterator
 * \param n     temporary storage
 * \param head	the list to iterate over
 *
 * This is safe to use if \a pos could be removed from the list in the body of
 * the loop.
 */
#define mtfs_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * Iterate over a list continuing after existing point
 * \param pos    the type * to use as a loop counter
 * \param head   the list head
 * \param member the name of the list_struct within the struct  
 */
#define mtfs_list_for_each_entry_continue(pos, head, member)                 \
        for (pos = mtfs_list_entry(pos->member.next, typeof(*pos), member);  \
             prefetch(pos->member.next), &pos->member != (head);            \
             pos = mtfs_list_entry(pos->member.next, typeof(*pos), member))

/**
 * \defgroup hlist Hash List
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is too
 * wasteful.  You lose the ability to access the tail in O(1).
 * @{
 */

typedef struct mtfs_hlist_node {
	struct mtfs_hlist_node *next, **pprev;
} mtfs_hlist_node_t;

typedef struct mtfs_hlist_head {
	mtfs_hlist_node_t *first;
} mtfs_hlist_head_t;

/* @} */

/*
 * "NULL" might not be defined at this point
 */
#ifdef NULL
#define MTFS_NULL_P NULL
#else
#define MTFS_NULL_P ((void *)0)
#endif

/**
 * \addtogroup hlist
 * @{
 */

#define MTFS_HLIST_HEAD_INIT { MTFS_NULL_P }
#define MTFS_HLIST_HEAD(name) mtfs_hlist_head_t name = { MTFS_NULL_P }
#define MTFS_INIT_HLIST_HEAD(ptr) ((ptr)->first = MTFS_NULL_P)
#define MTFS_INIT_HLIST_NODE(ptr) ((ptr)->next = MTFS_NULL_P, (ptr)->pprev = MTFS_NULL_P)

static inline int mtfs_hlist_unhashed(const mtfs_hlist_node_t *h)
{
	return !h->pprev;
}

static inline int mtfs_hlist_empty(const mtfs_hlist_head_t *h)
{
	return !h->first;
}

static inline void __mtfs_hlist_del(mtfs_hlist_node_t *n)
{
	mtfs_hlist_node_t *next = n->next;
	mtfs_hlist_node_t **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline void mtfs_hlist_del(mtfs_hlist_node_t *n)
{
	__mtfs_hlist_del(n);
}

static inline void mtfs_hlist_del_init(mtfs_hlist_node_t *n)
{
	if (n->pprev)  {
		__mtfs_hlist_del(n);
		MTFS_INIT_HLIST_NODE(n);
	}
}

static inline void mtfs_hlist_add_head(mtfs_hlist_node_t *n,
                                      mtfs_hlist_head_t *h)
{
	mtfs_hlist_node_t *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

/* next must be != NULL */
static inline void mtfs_hlist_add_before(mtfs_hlist_node_t *n,
					mtfs_hlist_node_t *next)
{
	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

static inline void mtfs_hlist_add_after(mtfs_hlist_node_t *n,
                                       mtfs_hlist_node_t *next)
{
	next->next = n->next;
	n->next = next;
	next->pprev = &n->next;

	if(next->next)
		next->next->pprev  = &next->next;
}

#define mtfs_hlist_entry(ptr, type, member) container_of(ptr,type,member)

#define mtfs_hlist_for_each(pos, head) \
	for (pos = (head)->first; pos && (prefetch(pos->next), 1); \
	     pos = pos->next)

#define mtfs_hlist_for_each_safe(pos, n, head) \
	for (pos = (head)->first; pos && (n = pos->next, 1); \
	     pos = n)

/**
 * Iterate over an hlist of given type
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param head	 the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define mtfs_hlist_for_each_entry(tpos, pos, head, member)                    \
	for (pos = (head)->first;                                            \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = mtfs_hlist_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing after existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define mtfs_hlist_for_each_entry_continue(tpos, pos, member)                 \
	for (pos = (pos)->next;                                              \
	     pos && ({ prefetch(pos->next); 1;}) &&                          \
		({ tpos = mtfs_hlist_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

/**
 * Iterate over an hlist continuing from an existing point
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param member the name of the hlist_node within the struct.
 */
#define mtfs_hlist_for_each_entry_from(tpos, pos, member)			 \
	for (; pos && ({ prefetch(pos->next); 1;}) &&                        \
		({ tpos = mtfs_hlist_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

/**
 * Iterate over an hlist of given type safe against removal of list entry
 * \param tpos	 the type * to use as a loop counter.
 * \param pos	 the &struct hlist_node to use as a loop counter.
 * \param n	 another &struct hlist_node to use as temporary storage
 * \param head	 the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define mtfs_hlist_for_each_entry_safe(tpos, pos, n, head, member)            \
	for (pos = (head)->first;                                            \
	     pos && ({ n = pos->next; 1; }) &&                               \
		({ tpos = mtfs_hlist_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = n)

/* @} */

#endif /* __linux__ && __KERNEL__ */

#ifndef mtfs_list_for_each_prev
/**
 * Iterate over a list in reverse order
 * \param pos	the &struct list_head to use as a loop counter.
 * \param head	the head for your list.
 */
#define mtfs_list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head);     \
		pos = pos->prev, prefetch(pos->prev))

#endif /* mtfs_list_for_each_prev */

#ifndef mtfs_list_for_each_entry
/**
 * Iterate over a list of given type
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define mtfs_list_for_each_entry(pos, head, member)                          \
        for (pos = mtfs_list_entry((head)->next, typeof(*pos), member),      \
		     prefetch(pos->member.next);                            \
	     &pos->member != (head);                                        \
	     pos = mtfs_list_entry(pos->member.next, typeof(*pos), member),  \
	     prefetch(pos->member.next))
#endif /* mtfs_list_for_each_entry */

#ifndef mtfs_list_for_each_entry_rcu
#define mtfs_list_for_each_entry_rcu(pos, head, member) \
       list_for_each_entry(pos, head, member)
#endif

#ifndef mtfs_list_for_each_entry_rcu
#define mtfs_list_for_each_entry_rcu(pos, head, member) \
       list_for_each_entry(pos, head, member)
#endif

#ifndef mtfs_list_for_each_entry_reverse
/**
 * Iterate backwards over a list of given type.
 * \param pos        the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define mtfs_list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = mtfs_list_entry((head)->prev, typeof(*pos), member);      \
	     prefetch(pos->member.prev), &pos->member != (head);            \
	     pos = mtfs_list_entry(pos->member.prev, typeof(*pos), member))
#endif /* mtfs_list_for_each_entry_reverse */

#ifndef mtfs_list_for_each_entry_safe
/**
 * Iterate over a list of given type safe against removal of list entry
 * \param pos        the type * to use as a loop counter.
 * \param n          another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define mtfs_list_for_each_entry_safe(pos, n, head, member)                   \
        for (pos = mtfs_list_entry((head)->next, typeof(*pos), member),       \
		n = mtfs_list_entry(pos->member.next, typeof(*pos), member);  \
	     &pos->member != (head);                                         \
	     pos = n, n = mtfs_list_entry(n->member.next, typeof(*n), member))

#endif /* mtfs_list_for_each_entry_safe */

#ifndef mtfs_list_for_each_entry_safe_from
/**
 * Iterate over a list continuing from an existing point
 * \param pos        the type * to use as a loop cursor.
 * \param n          another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define mtfs_list_for_each_entry_safe_from(pos, n, head, member)             \
        for (n = mtfs_list_entry(pos->member.next, typeof(*pos), member);    \
             &pos->member != (head);                                        \
             pos = n, n = mtfs_list_entry(n->member.next, typeof(*n), member))
#endif /* mtfs_list_for_each_entry_safe_from */

#define mtfs_list_for_each_entry_typed(pos, head, type, member)		\
        for (pos = mtfs_list_entry((head)->next, type, member),		\
		     prefetch(pos->member.next);                        \
	     &pos->member != (head);                                    \
	     pos = mtfs_list_entry(pos->member.next, type, member),	\
	     prefetch(pos->member.next))

#define mtfs_list_for_each_entry_reverse_typed(pos, head, type, member)	\
	for (pos = mtfs_list_entry((head)->prev, type, member);		\
	     prefetch(pos->member.prev), &pos->member != (head);	\
	     pos = mtfs_list_entry(pos->member.prev, type, member))

#define mtfs_list_for_each_entry_safe_typed(pos, n, head, type, member)	\
        for (pos = mtfs_list_entry((head)->next, type, member),		\
             n = mtfs_list_entry(pos->member.next, type, member);	\
             &pos->member != (head);                                    \
             pos = n, n = mtfs_list_entry(n->member.next, type, member))

#define mtfs_list_for_each_entry_safe_from_typed(pos, n, head, type, member)  \
        for (n = mtfs_list_entry(pos->member.next, type, member);             \
             &pos->member != (head);                                         \
             pos = n, n = mtfs_list_entry(n->member.next, type, member))

#define mtfs_hlist_for_each_entry_typed(tpos, pos, head, type, member)   \
	for (pos = (head)->first;                                       \
	     pos && (prefetch(pos->next), 1) &&                         \
		(tpos = mtfs_hlist_entry(pos, type, member), 1);         \
	     pos = pos->next)

#define mtfs_hlist_for_each_entry_safe_typed(tpos, pos, n, head, type, member) \
	for (pos = (head)->first;                                             \
	     pos && (n = pos->next, 1) &&                                     \
		(tpos = mtfs_hlist_entry(pos, type, member), 1);               \
	     pos = n)

/**
 * list_sort - sort a list.
 * @priv: private data, passed to @cmp
 * @head: the list to sort
 * @cmp: the elements comparison function
 *
 * This function has been implemented by Mark J Roberts <mjr@znex.org>. It
 * implements "merge sort" which has O(nlog(n)) complexity. The list is sorted
 * in ascending order.
 *
 * The comparison function @cmp is supposed to return a negative value if @a is
 * than @b, and a positive value if @a is greater than @b. If @a and @b are
 * equivalent, then it does not matter what this function returns.
 */
static inline void mtfs_list_sort(void *priv, mtfs_list_t *head,
                                  int (*cmp)(void *priv, mtfs_list_t *a,
                                  mtfs_list_t *b))
{
	mtfs_list_t *p, *q, *e, *list, *tail, *oldhead;
	int insize, nmerges, psize, qsize, i;

	if (mtfs_list_empty(head))
		return;

	list = head->next;
	mtfs_list_del(head);
	insize = 1;
	for (;;) {
		p = oldhead = list;
		list = tail = NULL;
		nmerges = 0;

		while (p) {
			nmerges++;
			q = p;
			psize = 0;
			for (i = 0; i < insize; i++) {
				psize++;
				q = q->next == oldhead ? NULL : q->next;
				if (!q)
					break;
			}

			qsize = insize;
			while (psize > 0 || (qsize > 0 && q)) {
				if (!psize) {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				} else if (!qsize || !q) {
					e = p;
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else if (cmp(priv, p, q) <= 0) {
					e = p;
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				}
				if (tail)
					tail->next = e;
				else
					list = e;
				e->prev = tail;
				tail = e;
			}
			p = q;
		}

		tail->next = list;
		list->prev = tail;

		if (nmerges <= 1)
			break;

		insize *= 2;
	}

	head->next = list;
	head->prev = list->prev;
	list->prev->next = head;
	list->prev = head;
}

#endif /* __MTFS_LIST_H__ */
