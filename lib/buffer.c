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
        if (!buffer_append(n, string, length)) {
                free(n);
                return NULL;
        }
        return n;
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

bool
buffer_append(struct buffer *buffer, const char *string, size_t length)
{
        if (length == 0)
                return true;
        // TODO Is this check needed?
        if (string == NULL)
                return false;
        if (!available(buffer, buffer->length + length + 1))
                return false;
        memcpy(buffer->content + buffer->length, string, length);
        buffer->length += length;
        return true;
}

bool
buffer_append_c(struct buffer *buffer, char c, size_t n)
{
        if (!available(buffer, buffer->length + n + 1))
                return false;
        memset(buffer->content + buffer->length, c, n);
        buffer->length += n;
        return true;
}

char *
buffer_str(struct buffer *buffer)
{
        resize(buffer, buffer->length + 1);
        buffer->content[buffer->length] = '\0';
        return buffer->content;
}
