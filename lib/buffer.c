#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <nmc/list.h>

#include <private.h>

#include "buffer.h"

struct buffer *
buffer_new(const char *string, size_t length)
{
        struct buffer *n = malloc(sizeof(struct buffer));
        n->length = n->allocated = 0;
        n->content = NULL;
        return buffer_append(n, string, length);
}

void
buffer_free(struct buffer *buffer)
{
        free(buffer->content);
        free(buffer);
}

static bool
resize(struct buffer *buffer, size_t n)
{
        char *t = realloc(buffer->content, n);
        if (t == NULL)
                return false;
        buffer->content = t;
        buffer->allocated = n;
        return true;
}

struct buffer *
buffer_append(struct buffer *buffer, const char *string, size_t length)
{
        if (string == NULL || length == 0)
                return buffer;

        size_t needed = buffer->length + length + 1;
        if (needed > buffer->allocated) {
                /* TODO libxml2 uses 4096 as default size.  We might want
                   something slightly larger here. */
                size_t n = buffer->allocated > 0 ? buffer->allocated * 2 : needed + 32;
                while (n < needed) {
                        if (n > SIZE_MAX / 2)
                                /* TODO Report error */
                                return buffer;
                        n *= 2;
                }
                if (!resize(buffer, n))
                        /* TODO Report error */
                        return buffer;
        }

        memcpy(buffer->content + buffer->length, string, length);
        buffer->length += length;

        return buffer;
}

char *
buffer_str(struct buffer *buffer)
{
        resize(buffer, buffer->length + 1);
        buffer->content[buffer->length] = '\0';
        return buffer->content;
}
