#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#define UNICODE_LAST_CHAR 0x10ffff

static void die(const char *message, ...) NORETURN;

static void
die(const char *message, ...)
{
        va_list args;
        va_start(args, message);
        vfprintf(stderr, message, args);
        va_end(args);
        exit(EXIT_FAILURE);
}

int
main(void)
{
        bool data[UNICODE_LAST_CHAR + 1];
        for (size_t i = 0; i < lengthof(data); i++)
                data[i] = false;
        char line[1024];
        unsigned int s, e;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                if (line[0] == '#' || line[0] == '\n')
                        continue;
                char padding[sizeof(line)], prop[sizeof(line)];
                if (sscanf(line, "%X..%X%[ ;]%[^ ]", &s, &e, padding, prop) != 4) {
                        if (sscanf(line, "%X%[ ;]%[^ ]", &s, padding, prop) != 3)
                                die("cannot parse line: %s\n", line);
                        e = s;
                }
                if (s > e)
                        die("start after end: %u > %u\n", s, e);
                if (e >= lengthof(data))
                        die("end beyond last Unicode character: %u %= %zu\n", e, lengthof(data));
                if (*prop == 'W' || *prop == 'F')
                        for (unsigned int i = s; i <= e; i++)
                                data[i] = true;
        }
        puts("static const struct uchar_interval wide[] = {");
        for (s = 0; s < lengthof(data); s++)
                if (data[s]) {
                        for (e = s; e + 1 < lengthof(data) && data[e + 1]; e++)
                                ;
                        printf("\t{ %#06x, %#06x },\n", s, e);
                        s = e;
                }
        puts("};");
        return EXIT_SUCCESS;
}
