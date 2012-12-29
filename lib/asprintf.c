#include <config.h>

#include <stdarg.h>
#include <stdbool.h>

#include <private.h>

int
asprintf(char **output, const char *format, ...)
{
        va_list args;
        va_start(args, format);
        int size = vasprintf(output, format, args);
        va_end(args);
        return size;
}
