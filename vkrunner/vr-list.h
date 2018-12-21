/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2012, 2013 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* This list implementation is based on the Wayland source code */

#ifndef VR_LIST_H
#define VR_LIST_H

#include <stddef.h>
#include "vr-util.h"

/**
 * vr_list - linked list
 *
 * The list head is of "vr_list" type, and must be initialized
 * using vr_list_init().  All entries in the list must be of the same
 * type.  The item type must have a "vr_list" member. This
 * member will be initialized by vr_list_insert(). There is no need to
 * call vr_list_init() on the individual item. To query if the list is
 * empty in O(1), use vr_list_empty().
 *
 * Let's call the list reference "vr_list foo_list", the item type as
 * "item_t", and the item member as "vr_list link". The following code
 *
 * The following code will initialize a list:
 *
 *      vr_list_init (foo_list);
 *      vr_list_insert (foo_list, item1);      Pushes item1 at the head
 *      vr_list_insert (foo_list, item2);      Pushes item2 at the head
 *      vr_list_insert (item2, item3);         Pushes item3 after item2
 *
 * The list now looks like [item2, item3, item1]
 *
 * Will iterate the list in ascending order:
 *
 *      item_t *item;
 *      vr_list_for_each(item, foo_list, link) {
 *              Do_something_with_item(item);
 *      }
 */

struct vr_list {
        struct vr_list *prev;
        struct vr_list *next;
};

void
vr_list_init(struct vr_list *list);

void
vr_list_insert(struct vr_list *list, struct vr_list *elm);

void
vr_list_remove(struct vr_list *elm);

int
vr_list_length(const struct vr_list *list);

int
vr_list_empty(const struct vr_list *list);

void
vr_list_insert_list(struct vr_list *list, struct vr_list *other);

#ifdef __cplusplus
#define VR_LIST_TYPECAST(iterator) (decltype(iterator))
#else
#define VR_LIST_TYPECAST(iterator) (void *)
#endif

/* This assigns to iterator first so that taking a reference to it
 * later in the second step won't be an undefined operation. It
 * assigns the value of list_node rather than 0 so that it is possible
 * have list_node be based on the previous value of iterator. In that
 * respect iterator is just used as a convenient temporary variable.
 * The compiler optimises all of this down to a single subtraction by
 * a constant */
#define vr_list_set_iterator(list_node, iterator, member)       \
        ((iterator) = VR_LIST_TYPECAST(iterator) (list_node),   \
         (iterator) = VR_LIST_TYPECAST(iterator)                \
         ((char *) (iterator) -                                 \
          (((char *) &(iterator)->member) -                     \
           (char *) (iterator))))

#define vr_container_of(ptr, type, member)                      \
        (type *) ((char *) (ptr) - offsetof (type, member))

#define vr_list_for_each(pos, head, member)                             \
        for (vr_list_set_iterator((head)->next, pos, member);           \
             &pos->member != (head);                                    \
             vr_list_set_iterator(pos->member.next, pos, member))

#define vr_list_for_each_safe(pos, tmp, head, member)                   \
        for (vr_list_set_iterator((head)->next, pos, member),           \
                     vr_list_set_iterator((pos)->member.next, tmp, member); \
             &pos->member != (head);                                    \
             pos = tmp,                                                 \
                     vr_list_set_iterator(pos->member.next, tmp, member))

#define vr_list_for_each_reverse(pos, head, member)                     \
        for (vr_list_set_iterator((head)->prev, pos, member);           \
             &pos->member != (head);                                    \
             vr_list_set_iterator(pos->member.prev, pos, member))

#endif /* VR_LIST_H */
