#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#define DATA_NAME "break"
#define DATA_NAME_UPPERCASE "BREAK"

struct dataname {
        const char *u;
        const char *c;
};

static const struct dataname names[] = {
      { "AI", "BREAK_AMBIGUOUS" },
      { "AL", "BREAK_ALPHABETIC" },
      { "B2", "BREAK_BEFORE_AND_AFTER" },
      { "BA", "BREAK_AFTER" },
      { "BB", "BREAK_BEFORE" },
      { "BK", "BREAK_MANDATORY" },
      { "CB", "BREAK_CONTINGENT" },
      { "CJ", "BREAK_CONDITIONAL_JAPANESE_STARTER" },
      { "CL", "BREAK_CLOSE_PUNCTUATION" },
      { "CM", "BREAK_COMBINING_MARK" },
      { "CP", "BREAK_CLOSE_PARENTHESIS" },
      { "CR", "BREAK_CARRIAGE_RETURN" },
      { "EX", "BREAK_EXCLAMATION" },
      { "GL", "BREAK_NON_BREAKING" },
      { "H2", "BREAK_HANGUL_LV_SYLLABLE" },
      { "H3", "BREAK_HANGUL_LVT_SYLLABLE" },
      { "HL", "BREAK_HEBREW_LETTER" },
      { "HY", "BREAK_HYPHEN" },
      { "ID", "BREAK_IDEOGRAPHIC" },
      { "IN", "BREAK_INSEPARABLE" },
      { "IS", "BREAK_INFIX_NUMERIC_SEPARATOR" },
      { "JL", "BREAK_HANGUL_L_JAMO" },
      { "JT", "BREAK_HANGUL_T_JAMO" },
      { "JV", "BREAK_HANGUL_V_JAMO" },
      { "LF", "BREAK_LINE_FEED" },
      { "NL", "BREAK_NEXT_LINE" },
      { "NS", "BREAK_NON_STARTER" },
      { "NU", "BREAK_NUMERIC" },
      { "OP", "BREAK_OPEN_PUNCTUATION" },
      { "PO", "BREAK_POSTFIX_NUMERIC" },
      { "PR", "BREAK_PREFIX_NUMERIC" },
      { "QU", "BREAK_QUOTATION" },
      { "RI", "BREAK_REGIONAL_INDICATOR" },
      { "SA", "BREAK_COMPLEX_CONTEXT_DEPENDENT" },
      { "SG", "BREAK_SURROGATE" },
      { "SP", "BREAK_SPACE" },
      { "SY", "BREAK_SYMBOLS_ALLOWING_BREAK_AFTER" },
      { "WJ", "BREAK_WORD_JOINER" },
      { "XX", "BREAK_UNKNOWN" },
      { "ZW", "BREAK_ZERO_WIDTH_SPACE" },
};

#include "udata.h"

int
main(void)
{
        const char *xx = data_name("XX");
        int last_char_part_1 = -1;
        char line[1024];
        int previous = -1;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                char *p = line;
                if (*p == '#' || *p == '\n')
                        continue;
                char *end;
                int s = (int)strtol(strsep(&p, ";"), &end, 16);
                int e = *end == '.' ? (int)strtol(end + 2, NULL, 16) : s;
                if (s >= UNICODE_FIRST_CHAR_PART_2 && previous < UNICODE_FIRST_CHAR_PART_2)
                        last_char_part_1 = ((previous >> 8) + 1) * 256 - 1;
                const char *name = data_name(strsep(&p, " "));
                if (s > previous + 1) {
                        for (int i = previous + 1; i < s; i++)
                                data[i] = xx;
                }
                for (int i = s; i <= e; i++)
                        data[i] = name;
                previous = e;
        }
        for (int i = previous + 1; i <= UNICODE_LAST_CHAR; i++)
                data[i] = xx;
        parts(last_char_part_1);
        return EXIT_SUCCESS;
}
