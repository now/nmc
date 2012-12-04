#include <config.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <private.h>

#include "ext.h"

int
nmc_vasprintf(char **output, const char *format, va_list args)
{
        va_list saved;
        va_copy(saved, args);
        char buf[1];
        int size = vsnprintf(buf, sizeof(buf), format, args);
        char *result = malloc(size + 1);
        vsnprintf(result, size + 1, format, saved);
        va_end(saved);
        *output = result;
        return size;
}

int
nmc_asprintf(char **output, const char *format, ...)
{
        va_list args;
        va_start(args, format);
        int size = nmc_vasprintf(output, format, args);
        va_end(args);
        return size;
}
