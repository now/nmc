#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <private.h>

#include "unicode.h"

#define UNICODE_LAST_CHAR 0x10ffff

#include "ubreak.h"
#include "ucategory.h"
#include "uscript.h"

#define UNICODE_FIRST_CHAR_PART_2 0xe0000

#define IS(type, class) (((unsigned int)1 << (type)) & (class))
#define OR(type, rest)  (((unsigned int)1 << (type)) | (rest))

static inline int
lookup(uchar c, uchar last, uchar missing, uint8_t max, const uint8_t part_1[], const uint8_t part_2[], const int8_t data[][256])
{
        int16_t index;

	if (c <= last)
                index = part_1[c >> 8];
        else if (UNICODE_FIRST_CHAR_PART_2 <= c && c <= UNICODE_LAST_CHAR)
                index = part_2[(c - UNICODE_FIRST_CHAR_PART_2) >> 8];
	else
		return missing;

	if (index >= max)
		return index - max;

        return data[index][c & 0xff];
}

static inline int
s_type(uchar c)
{
        return lookup(c,
                      UNICODE_LAST_CHAR_CATEGORY_PART_1,
                      UNICODE_OTHER_NOT_ASSIGNED,
                      UNICODE_CATEGORY_DATA_MAX_INDEX,
                      category_pages_part_1,
                      category_pages_part_2,
                      category_data);
}

bool
uc_issolid(uchar c)
{
        // TODO We should really check the combining class of c as well.
        return !IS(s_type(c),
                   OR(UNICODE_OTHER_CONTROL,
                      OR(UNICODE_OTHER_FORMAT,
                         OR(UNICODE_OTHER_SURROGATE,
                            OR(UNICODE_OTHER_PRIVATE_USE,
                               OR(UNICODE_OTHER_NOT_ASSIGNED,
                                  OR(UNICODE_MARK_SPACING_COMBINING,
                                     OR(UNICODE_MARK_ENCLOSING,
                                        OR(UNICODE_MARK_NONSPACING,
                                           OR(UNICODE_SEPARATOR_LINE,
                                              OR(UNICODE_SEPARATOR_PARAGRAPH,
                                                 OR(UNICODE_SEPARATOR_SPACE, 0))))))))))));
}

static inline int
s_break(uchar c)
{
        return lookup(c,
                      UNICODE_LAST_CHAR_BREAK_PART_1,
                      UNICODE_OTHER_NOT_ASSIGNED,
                      UNICODE_BREAK_DATA_MAX_INDEX,
                      break_pages_part_1,
                      break_pages_part_2,
                      break_data);
}

static inline enum unicode_script
uc_script_bsearch(uchar c)
{
	size_t begin = 0;
        size_t end = lengthof(script_table) - 1;
        static size_t cached_middle = lengthof(script_table) / 2;
        size_t middle = cached_middle;

        do {
                uchar probe = script_table[middle].start;
                if (c < probe)
                        end = middle - 1;
                else if (c >= probe + script_table[middle].chars)
                        begin = middle + 1;
                else
                        return script_table[cached_middle = middle].script;
                middle = (begin + end) >> 1;
        } while (begin <= end);

        return UNICODE_SCRIPT_UNKNOWN;
}

static inline enum unicode_script
uc_script(uchar c)
{
        return c < UNICODE_SCRIPT_EASY_TABLE_MAX_INDEX ?
                script_easy_table[c] :
                uc_script_bsearch(c);
}

bool
uc_isaletterornumeric(uchar c)
{
        int break_type = s_break(c);
        if (break_type == UNICODE_BREAK_NUMERIC && c != 0x066c)
                return true;
        int type = s_type(c);
        if (IS(type,
               OR(UNICODE_LETTER_LOWERCASE,
                  OR(UNICODE_LETTER_TITLECASE,
                     OR(UNICODE_LETTER_UPPERCASE, 0))))) {
                enum unicode_script script;
        Alphabetic:
                script = uc_script(c);
                return break_type != UNICODE_BREAK_COMPLEX_CONTEXT_DEPENDENT &&
                        script != UNICODE_SCRIPT_KATAKANA &&
                        script != UNICODE_SCRIPT_HIRAGANA;
        } else if (type == UNICODE_LETTER_MODIFIER) {
                if ((0x3031 <= c && c <= 0x3035) ||
                    c == 0x309b || c == 0x309c || c == 0x30a0 || c == 0x30fc ||
                    c == 0xff70 ||
                    c == 0xff9e || c == 0xff9f)
                        return false; // Katakana exceptions
                else
                        goto Alphabetic;
        } else if (IS(type,
                      OR(UNICODE_LETTER_OTHER,
                         OR(UNICODE_NUMBER_LETTER, 0)))) {
                if (c == 0x3006 || c == 0x3007 ||
                    (0x3021 <= c && c <= 0x3029) ||
                    (0x3038 <= c && c <= 0x303a) ||
                    (0x3400 <= c && c <= 0x4db5) ||
                    (0x4e00 <= c && c <= 0x9fc3) ||
                    (0xf900 <= c && c <= 0xfa2d) ||
                    (0xfa30 <= c && c <= 0xfa6a) ||
                    (0xfa70 <= c && c <= 0xfad9) ||
                    (0x20000 <= c && c <= 0x2a6d6) ||
                    (0x2f800 <= c && c <= 0x2fa1d)) // Ideographic (PropList)
                        return false;
                else
                        goto Alphabetic;
        } else if (type == UNICODE_SYMBOL_OTHER) {
                if (0x24b6 <= c && c <= 0x24e9) // Other_Alphabetic (PropList)
                        goto Alphabetic;
                else
                        return false;
        } else if (c == 0x05f3)
                return true;

        return false;
}

// The dfa table and decode function is © 2008–2010 Björn Höhrmann
// <bjoern@hoehrmann.de>.  See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
// for details.

enum {
        ACCEPT = 0,
        REJECT = 12
};

static const uint8_t dfa[] = {
        // The first part of the table maps bytes to character classes to
        // reduce the size of the transition table and create bitmasks.
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
       10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

        // The second part is a transition table that maps a combination
        // of a state of the automaton and a character class to a state.
        0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
       12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
       12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
       12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
       12,36,12,12,12,12,12,12,12,12,12,12,
};

static inline uchar
decode(uchar *state, uchar *c, uchar b)
{
        uchar type = dfa[b];
        *c = *state != ACCEPT ? (*c << 6) | (b & 0x3f) : (0xff >> type) & b;
        *state = dfa[256 + *state + type];
        return *state;
}

uchar
u_dref(const char *u)
{
        uchar c, state = ACCEPT;
        for (const unsigned char *p = (const unsigned char *)u; *p != '\0'; p++)
                switch (decode(&state, &c, *p)) {
                case ACCEPT:
                        return c;
                case REJECT:
                        return U_BAD_INPUT_CHAR;
                }
        return *u == '\0' ? '\0' : U_BAD_INPUT_CHAR;
}

static const uint8_t s_u_skip_length_data[256] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 1, 1
};

const char * const u_skip_lengths = (const char *)s_u_skip_length_data;
