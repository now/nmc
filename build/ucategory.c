#include <config.h>

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#define DATA_NAME "category"
#define DATA_NAME_UPPERCASE "CATEGORY"

struct dataname {
        const char *u;
        const char *c;
};

static const struct dataname names[] = {
        { "Cc", "OTHER_CONTROL" },
        { "Cf", "OTHER_FORMAT" },
        { "Cn", "OTHER_NOT_ASSIGNED" },
        { "Co", "OTHER_PRIVATE_USE" },
        { "Cs", "OTHER_SURROGATE" },
        { "Ll", "LETTER_LOWERCASE" },
        { "Lm", "LETTER_MODIFIER" },
        { "Lo", "LETTER_OTHER" },
        { "Lt", "LETTER_TITLECASE" },
        { "Lu", "LETTER_UPPERCASE" },
        { "Mc", "MARK_SPACING_COMBINING" },
        { "Me", "MARK_ENCLOSING" },
        { "Mn", "MARK_NONSPACING" },
        { "Nd", "NUMBER_DECIMAL_DIGIT" },
        { "Nl", "NUMBER_LETTER" },
        { "No", "NUMBER_OTHER" },
        { "Pc", "PUNCTUATION_CONNECTOR" },
        { "Pd", "PUNCTUATION_DASH" },
        { "Pe", "PUNCTUATION_CLOSE" },
        { "Pf", "PUNCTUATION_FINAL_QUOTE" },
        { "Pi", "PUNCTUATION_INITIAL_QUOTE" },
        { "Po", "PUNCTUATION_OTHER" },
        { "Ps", "PUNCTUATION_OPEN" },
        { "Sc", "SYMBOL_CURRENCY" },
        { "Sk", "SYMBOL_MODIFIER" },
        { "Sm", "SYMBOL_MATH" },
        { "So", "SYMBOL_OTHER" },
        { "Zl", "SEPARATOR_LINE" },
        { "Zp", "SEPARATOR_PARAGRAPH" },
        { "Zs", "SEPARATOR_SPACE" },
};

#include "udata.h"

#define LAST " Last>"

int
main(void)
{
        const char *cn = data_name("Cn");
        int last_char_part_1 = -1;
        char line[1024];
        int previous = -1;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                char *p = line;
                int c = (int)strtol(strsep(&p, ";"), NULL, 16);
                if (c >= UNICODE_FIRST_CHAR_PART_2 && previous < UNICODE_FIRST_CHAR_PART_2)
                        last_char_part_1 = ((previous >> 8) + 1) * 256 - 1;
                char *name = strsep(&p, ";");
                const char *category = data_name(strsep(&p, ";"));
                if (c > previous + 1 && strcmp(name + strlen(name) + 1 - sizeof(LAST), LAST) != 0) {
                        for (int i = previous + 1; i < c; i++)
                                data[i] = cn;
                        data[c] = category;
                } else
                        for (int i = previous + 1; i <= c; i++)
                                data[i] = category;
                previous = c;
        }
        for (int i = previous + 1; i <= UNICODE_LAST_CHAR; i++)
                data[i] = cn;
        parts(last_char_part_1);
        return EXIT_SUCCESS;
}
