#define list_for_each(type, item, list) \
        for (type *item = list; item != NULL; item = item->next)

#define list_for_each_safe(type, item, n, list) \
        for (type *item = list, *n = item != NULL ? item->next : NULL; item != NULL; item = n, n = n != NULL ? n->next : NULL)
