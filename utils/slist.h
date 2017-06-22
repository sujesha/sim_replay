#ifndef _SLIST_H_
#define _SLIST_H_

#include <stdio.h>

#ifndef offsetof
/**
 * Get offset of a member
 */
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
/**
 * Casts a member of a structure out to the containing structure
 * @param ptr        the pointer to the member.
 * @param type       the type of the container struct this is embedded in.
 * @param member     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized list entries.
 */
#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

struct slist_head {
	struct slist_head *next; //, *prev;
};

#define LIST_HEAD_INIT(name) { &(name) } //, &(name) }

#define LIST_HEAD(name) \
	struct slist_head name = LIST_HEAD_INIT(name)

/** INIT_LIST_HEAD -- initialize the single-linked list */
static inline void INIT_LIST_HEAD(struct slist_head *list)
{
	list->next = list;
}

/** __slist_add -- Insert a new entry between two known consecutive entries.
 * 			This is only for internal list manipulation where we know
 * 			the prev/next entries already! i.e. from slist_add().
 *
 * @new[in]: list in which element to be added
 * @prev[in]: previous element to the position where element to be added
 * @next[in]: next element to the position where element to be added
 */
static inline void __slist_add(struct slist_head *new,
			      struct slist_head *prev,
			      struct slist_head *next)
{
	new->next = next;
	prev->next = new;
}

/**
 * slist_add - add a new entry
 * 		Insert a new entry after the specified head. For implementing stacks.
 *
 * @new: new entry to be added
 * @head: list head to add it after
 */
static inline void slist_add(struct slist_head *new, struct slist_head *head)
{
	__slist_add(new, head, head->next);
}

#if 0
/**
 * slist_add_tail - add a new entry
 * @new: new entry to be added
 * @head: slist head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void slist_add_tail(struct slist_head *new, struct slist_head *head)
{
	__slist_add(new, head->prev, head);
}
#endif

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __slist_del(struct slist_head *prev, struct slist_head *next)
{
	//next->prev = prev;
	prev->next = next;
}

/**
 * __slist_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 *
 * This variant differs from slist_for_each() in that it's the
 * simplest possible list iteration code, no prefetching is done.
 * Use this for code that knows the list to be very short (empty
 * or 1 entry) most of the time.
 */
#define __slist_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * slist_for_each_safe	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct slist_head to use as a loop counter.
 * @n:		another &struct slist_head to use as temporary storage
 * @head:	the head for your list.
 */
#define slist_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * slist_entry - get the struct for this entry
 * @ptr:        the &struct slist_head pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the slist_struct within the struct.
 */
#define slist_entry(ptr, type, member) \
        container_of(ptr, type, member)

/**
 * slist_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void slist_del(struct slist_head *entry, struct slist_head *head)
{
	struct slist_head *pos, *prev;
	prev = head;

	__slist_for_each(pos, head)
	{
		if (pos == entry)
			break;
		prev = pos;
	}
	__slist_del(prev, entry->next);
	entry->next = LIST_POISON1;
	//entry->prev = LIST_POISON2;
}

static inline int slist_len(struct slist_head *head_p)
{
	struct slist_head *p;
	int n = 0;

	__slist_for_each(p, head_p) {
		n++;
	}

	return n;
}

/**
 * slist_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int slist_empty(const struct slist_head *head)
{
	return head->next == head;
}

/**
 * slist_first - Returns first entry on list, or NULL if empty
 * @head: the list
 */
static inline struct slist_head *slist_first(const struct slist_head *head)
{
	return slist_empty(head) ? NULL : head->next;
}

#endif /* _SLIST_H_ */
