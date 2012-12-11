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

static bool
available(struct buffer *buffer, size_t size)
{
        if (size <= buffer->allocated)
                return true;
        // TODO libxml2 uses 4096 as default size.  We might want something
        // slightly larger here.
        size_t n = buffer->allocated > 0 ? buffer->allocated * 2 : size + 32;
        while (n < size) {
                if (n > SIZE_MAX / 2)
                        return false;
                n *= 2;
        }
        return resize(buffer, n);
}

struct buffer *
buffer_append(struct buffer *buffer, const char *string, size_t length)
{
        // TODO Is this check needed?
        if (string == NULL || length == 0)
                return buffer;
        if (!available(buffer, buffer->length + length + 1))
                return buffer;
        memcpy(buffer->content + buffer->length, string, length);
        buffer->length += length;
        return buffer;
}

struct buffer *
buffer_append_c(struct buffer *buffer, char c, size_t n)
{
        if (!available(buffer, buffer->length + n + 1))
                return buffer;
        memset(buffer->content + buffer->length, c, n);
        buffer->length += n;
        return buffer;
}

char *
buffer_str(struct buffer *buffer)
{
        resize(buffer, buffer->length + 1);
        buffer->content[buffer->length] = '\0';
        return buffer->content;
}
