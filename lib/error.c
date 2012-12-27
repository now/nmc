#include <config.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <nmc.h>
#include <nmc/list.h>

#include <private.h>

#include "error.h"
#include "ext.h"

struct nmc_parser_error nmc_parser_oom_error = {
        NULL,
        { 0, 0, 0, 0 },
        (char *)"memory exhausted"
};

void
nmc_parser_error_free(struct nmc_parser_error *error)
{
        list_for_each_safe(struct nmc_parser_error, p, n, error) {
                if (p != &nmc_parser_oom_error) {
                        free(p->message);
                        free(p);
                }
        }
}

struct nmc_parser_error *
nmc_parser_error_newv(struct nmc_location *location, const char *message, va_list args)
{
        struct nmc_parser_error *error = malloc(sizeof(struct nmc_parser_error));
        if (error == NULL)
                return NULL;
        error->next = NULL;
        error->location = *location;
        if (nmc_vasprintf(&error->message, message, args) == -1) {
                free(error);
                return NULL;
        }
        return error;
}

struct nmc_parser_error *
nmc_parser_error_new(struct nmc_location *location, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        struct nmc_parser_error *error = nmc_parser_error_newv(location, message, args);
        va_end(args);
        return error;
}

static void
nmc_error_release_static(UNUSED(struct nmc_error *error))
{
}

static void
nmc_error_release_dynamic(struct nmc_error *error)
{
        free(error->message);
}

static struct nmc_error nmc_oom_error = {
        nmc_error_release_static,
        ENOMEM,
        (char *)""
};

static inline bool
init(struct nmc_error *error, void (*release)(struct nmc_error *), int number,
     char *message)
{
        error->release = release;
        error->number = number;
        error->message = message;
        return false;
}

bool
nmc_error_init(struct nmc_error *error, int number, const char *message)
{
        return init(error, nmc_error_release_static, number, (char *)message);
}

bool
nmc_error_oom(struct nmc_error *error)
{
        return init(error, nmc_oom_error.release, nmc_oom_error.number, nmc_oom_error.message);
}

bool
nmc_error_dyninit(struct nmc_error *error, int number, char *message)
{
        return message == NULL ? nmc_error_oom(error) :
                init(error, nmc_error_release_dynamic, number, message);
}

bool
nmc_error_formatv(struct nmc_error *error, int number, const char *message, va_list args)
{
        char *formatted;
        nmc_vasprintf(&formatted, message, args);
        return nmc_error_dyninit(error, number, formatted);
}

bool
nmc_error_format(struct nmc_error *error, int number, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        bool r = nmc_error_formatv(error, number, message, args);
        va_end(args);
        return r;
}

void
nmc_error_release(struct nmc_error *error)
{
        error->release(error);
}
