#include <config.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <nmc.h>

#include <private.h>

#include "error.h"

bool
nmc_output_write_all(struct nmc_output *output, const char *string,
                     size_t length, size_t *written, struct nmc_error *error)
{
        size_t n = 0;
        while (n < length) {
                ssize_t w = output->write(output, string + n, length - n, error);
                if (w == -1) {
                        *written = n;
                        return false;
                }
                n += w;
        }
        *written = n;
        return true;
}

bool
nmc_output_close(struct nmc_output *output, struct nmc_error *error)
{
        return output->close == NULL || output->close(output, error);
}

static ssize_t
nmc_fd_output_write(struct nmc_fd_output *output, const char *string,
                    size_t length, struct nmc_error *error)
{
        if (length == 0)
                return 0;
        while (true) {
                ssize_t w = write(output->fd, string, length);
                if (w == -1) {
                        if (errno == EAGAIN || errno == EINTR)
                                continue;
                        nmc_error_init(error, errno, "can’t write to file");
                }
                return w;
        }
}

void
nmc_fd_output_init(struct nmc_fd_output *output, int fd)
{
        output->output.write = (nmc_output_write_fn)nmc_fd_output_write;
        output->output.close = NULL;
        output->fd = fd;
}

static bool
flush(struct nmc_buffered_output *output, struct nmc_error *error)
{
        size_t w;
        bool r = nmc_output_write_all(output->real, output->buffer,
                                      output->length, &w, error);
        if (w < output->length)
                memmove(output->buffer, output->buffer + w, output->length - w);
        output->length -= w;
        return r;
}

static ssize_t
nmc_buffered_output_write(struct nmc_buffered_output *output,
                          const char *string, size_t length,
                          struct nmc_error *error)
{
        if (output->length == sizeof(output->buffer) && !flush(output, error))
                return -1;
        size_t r = sizeof(output->buffer) - output->length;
        size_t n = r < length ? r : length;
        memcpy(output->buffer + output->length, string, n);
        output->length += n;
        return n;
}

static bool
nmc_buffered_output_close(struct nmc_buffered_output *output,
                          struct nmc_error *error)
{
        if (flush(output, error))
                return nmc_output_close(output->real, error);
        struct nmc_error ignored;
        nmc_output_close(output->real, &ignored);
        return false;
}

void
nmc_buffered_output_init(struct nmc_buffered_output *output, struct nmc_output *real)
{
        output->output.write = (nmc_output_write_fn)nmc_buffered_output_write;
        output->output.close = (nmc_output_close_fn)nmc_buffered_output_close;
        output->real = real;
        output->length = 0;
}
