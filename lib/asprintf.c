#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <nmc.h>

#include <private.h>

#include "ext.h"

int
asprintf(char **output, const char *format, ...)
{
        va_list args;
        va_start(args, format);
        int size = nmc_vasprintf(output, format, args);
        va_end(args);
        return size;
}
