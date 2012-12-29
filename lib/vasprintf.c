#include <config.h>

#include <stdarg.h>
#include <stdbool.h>

#include <private.h>

int
vasprintf(char **output, const char *format, va_list args)
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
