#include <config.h>

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
