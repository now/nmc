#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#define DATA_NAME "word_break"
#define DATA_NAME_UPPERCASE "WORD_BREAK"

struct dataname {
        const char *u;
        const char *c;
};

static const struct dataname names[] = {
      { "ALetter",            "WORD_BREAK_ALETTER" },
      { "CR",                 "WORD_BREAK_CR" },
      { "Extend",             "WORD_BREAK_EXTEND" },
      { "ExtendNumLet",       "WORD_BREAK_EXTENDNUMLET" },
      { "Format",             "WORD_BREAK_FORMAT" },
      { "Katakana",           "WORD_BREAK_KATAKANA" },
      { "LF",                 "WORD_BREAK_LF" },
      { "MidLetter",          "WORD_BREAK_MIDLETTER" },
      { "MidNum",             "WORD_BREAK_MIDNUM" },
      { "MidNumLet",          "WORD_BREAK_MIDNUMLET" },
      { "Newline",            "WORD_BREAK_NEWLINE" },
      { "Numeric",            "WORD_BREAK_NUMERIC" },
      { "Other",              "WORD_BREAK_OTHER" },
      { "Regional_Indicator", "WORD_BREAK_REGIONAL_INDICATOR" },
};

#include "udata.h"

int
main(void)
{
        const char *xx = data_name("Other");
        for (size_t i = 0; i < lengthof(data); i++)
                data[i] = xx;
        int last_char_part_1 = 0;
        char line[1024];
        while (fgets(line, sizeof(line), stdin) != NULL) {
                if (line[0] == '#' || line[0] == '\n')
                        continue;
                unsigned int s, e;
                char padding[sizeof(line)], uname[sizeof(line)];
                if (sscanf(line, "%X..%X%[ ;]%[^ ]", &s, &e, padding, uname) != 4) {
                        if (sscanf(line, "%X%[ ;]%[^ ]", &s, padding, uname) != 3)
                                die("cannot parse line: %s\n", line);
                        e = s;
                }
                if (s > e)
                        die("start after end: %u > %u\n", s, e);
                if (e >= lengthof(data))
                        die("end beyond last Unicode character: %u >= %zu\n", e, lengthof(data));
                for (unsigned int i = s; i <= e; i++)
                        data[i] = data_name(uname);
        }
        for (int i = UNICODE_FIRST_CHAR_PART_2 - 1; i > 0; i--)
                if (data[i] != xx) {
                        last_char_part_1 = i;
                        break;
                }
        parts(last_char_part_1);
        return EXIT_SUCCESS;
}
