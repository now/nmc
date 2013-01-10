#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#include <unicode.h>

static int
uchar_to_utf8(uchar c, char *b)
{
        int s, n;
        if (c < 0x80) { s = 0; n = 1; }
        else if (c < 0x800) { s = 0xc0; n = 2; }
        else if (c < 0x10000) { s = 0xe0; n = 3; }
        else { s = 0xf0; n = 4; }
        for (int i = n - 1; i > 0; i--) {
                b[i] = (c & 0x3f) | 0x80;
                c >>= 6;
        }
        b[0] = c | s;
        return n;
}

static void
output_breaks(const bool *breaks, const char *chars)
{
        const char *p;
        for (p = chars; *p != '\0'; p = u_next(p)) {
                fputs(breaks[p - chars] ? "÷" : "×", stderr);
                fprintf(stderr, " %04X ", u_dref(p));
        }
        fputs(breaks[p - chars] ? "÷" : "×", stderr);
}

int
main(void)
{
        char line[1024];
        int failures = 0;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                char *p = line;
                if (*p == '#' || *p == '\n')
                        continue;
                bool expected[lengthof(line)];
                memset(expected, 0, sizeof(expected));
                char chars[4 * lengthof(line)];
                char *q = chars;
                while (true) {
                        char *s = strsep(&p, " \t");
                        if (*s == '#' || p == NULL)
                                break;
                        expected[q - chars] = u_dref(s) == 0xf7;
                        char b[4];
                        s = strsep(&p, " \t");
                        if (*s == '#' || p == NULL)
                                break;
                        q += uchar_to_utf8(strtol(s, NULL, 16), q);
                }
                *q = '\0';
                bool actual[lengthof(expected)];
                u_word_breaks(chars, q - chars, actual);
                actual[q - chars] = true;
                for (const char *r = chars; *r != '\0'; r = u_next(r))
                        if (actual[r - chars] != expected[r - chars]) {
                                failures++;
                                output_breaks(actual, chars);
                                fputs("≠", stderr);
                                output_breaks(expected, chars);
                                fputc('\n', stderr);
                                break;
                        }
        }
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
