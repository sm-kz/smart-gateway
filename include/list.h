#ifndef __LIST_H
#define __LIST_H

struct list_head {
    struct list_head *prev, *next;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/* 将 `new` 添加到 `head` 之后 */
static inline void list_add(struct list_head *new, struct list_head *head) {
    __list_add(new, head, head->next);
}

/* 将 `new` 添加到 `head` 链表的尾部 */
static inline void list_add_tail(struct list_head *new, struct list_head *head) {
	__list_add(new, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next) {
	next->prev = prev;
	prev->next = next;
}

static inline void list_del(struct list_head *entry) {
	__list_del(entry->prev, entry->next);
}

static inline void list_del_init(struct list_head *entry) {
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

static inline void list_replace(struct list_head *old, struct list_head *new) {
	new->prev = old->prev;
	new->next = old->next;
	new->prev->next = new;
	new->next->prev = new;
}

static inline void list_replace_init(struct list_head *old, struct list_head *new) {
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

/* 将 `entry` 从一个链表中移动到另一个链表 `head` 之后 */
static inline void list_move(struct list_head *entry, struct list_head *head) {
	list_del(entry);
	list_add(entry, head);
}

/* 将 `entry` 从一个链表中移动到另一个链表 `head` 的尾部 */
static inline void list_move_tail(struct list_head *entry, struct list_head *head) {
	list_del(entry);
	list_add_tail(entry, head);
}

static inline int list_is_last(const struct list_head *entry,
				const struct list_head *head) {
	return entry->next == head;
}

static inline int list_empty(const struct list_head *head) {
	return head->next == head;
}

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

/**
 * 获取包含指定链表项的结构体对象
 * @ptr: 	`struct list_head` 指针
 * @type:	列表项嵌入的对象类型
 * @member:	列表项在对应结构体类型中的成员名
 */
#define list_entry(ptr, type, member)		container_of(ptr, type, member)
#define list_first_entry(ptr, type, member)	list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member)	list_entry((ptr)->prev, type, member)
#define list_prev_entry(pos, member)		list_entry((pos)->member.prev, typeof(*(pos)), member)
#define list_next_entry(pos, member)		list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_foreach(pos, head)	\
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_foreach_entry(pos, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member); \
		 &pos->member != head; \
		 pos = list_next_entry(pos, member))
		 
#endif