#ifndef SRC_LIST_H
#define SRC_LIST_H

#define list_cons(item, list) (item->next = list, item)

#define list_shift(item, list) (item = list, list = list->next)

#define list_for_each(type, item, list) \
        for (type *item = list; item != NULL; item = item->next)

#define list_for_each_safe(type, item, n, list) \
        for (type *item = list, *n = item != NULL ? item->next : NULL; item != NULL; item = n, n = n != NULL ? n->next : NULL)

#define list_reverse(type, list) (type *)list_reverse_(list)

static inline void *
list_reverse_(void *list)
{
        void *reverse = NULL;
        while (list != NULL) {
                void *p = list;
                list = (void *)*(char **)p;
                *(char **)p = (char *)reverse;
                reverse = p;
        }
        return reverse;
}

#endif
