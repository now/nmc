#include <config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nmc/list.h>

#include <private.h>

#include "buffer.h"

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

static inline ssize_t
read1(struct buffer *buffer, int fd)
{
        while (true) {
                ssize_t r = read(fd, buffer->content + buffer->length,
                                 buffer->allocated - buffer->length - 1);
                if (r == -1) {
                        if (errno == EAGAIN || errno == EINTR)
                                continue;
                } else
                        buffer->length += r;
                return r;
        }
}

bool
buffer_read(struct buffer *buffer, int fd, size_t n)
{
        struct stat s;
        if (n > 0 || (fstat(fd, &s) != -1 && (n = s.st_size) > 0)) {
                // NOTE Add an additional byte so that the second call to
                // read() returns 0 at EOF, not because the buffer’s full.
                if (!resize(buffer, buffer->length + n + 1 + 1) ||
                    read1(buffer, fd) == -1)
                        return false;
        } else if (!available(buffer, buffer->length + 8192 + 1))
                return false;
        while (true) {
                ssize_t r = read1(buffer, fd);
                if (r == -1)
                        return false;
                else if (r == 0)
                        return true;
                buffer->length += r;
                if (!available(buffer, buffer->length + 8192 + 1))
                        return false;
        }
}

char *
buffer_str(struct buffer *buffer)
{
        resize(buffer, buffer->length + 1);
        buffer->content[buffer->length] = '\0';
        return buffer->content;
}
