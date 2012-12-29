#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <private.h>

#include "unicode.h"

#define UNICODE_LAST_CHAR 0x10ffff
#define UNICODE_MAX_TABLE_INDEX ((UNICODE_LAST_CHAR + 1) / 256)

#include "ucategory.h"

#define UNICODE_FIRST_CHAR_PART_2 0xe0000
#define UNICODE_LAST_PAGE_PART_1 (UNICODE_LAST_CHAR_PART_1 / 256)
#define UNICODE_LAST_PAGE_PART_2 ((UNICODE_LAST_CHAR + 1 - UNICODE_FIRST_CHAR_PART_2) / 256)

#define IS(type, class) (((unsigned int)1 << (type)) & (class))
#define OR(type, rest)  (((unsigned int)1 << (type)) | (rest))

static inline int
s_type(uchar c)
{
        int16_t index;

	if (c <= UNICODE_LAST_CHAR_PART_1)
                index = category_pages_part_1[c >> 8];
        else if (UNICODE_FIRST_CHAR_PART_2 <= c && c <= UNICODE_LAST_CHAR)
                index = category_pages_part_2[(c - UNICODE_FIRST_CHAR_PART_2) >> 8];
	else
		return UNICODE_OTHER_NOT_ASSIGNED;

	if (index >= UNICODE_MAX_TABLE_INDEX)
		return index - UNICODE_MAX_TABLE_INDEX;

        return category_data[index][c & 0xff];
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
