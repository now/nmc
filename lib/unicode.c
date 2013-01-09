#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <private.h>

#include "unicode.h"

#define UNICODE_LAST_CHAR 0x10ffff

#include "ucategory.h"
#include "uwordbreak.h"

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
s_word_break(uchar c)
{
        return lookup(c,
                      UNICODE_LAST_CHAR_WORD_BREAK_PART_1,
                      UNICODE_WORD_BREAK_OTHER,
                      UNICODE_WORD_BREAK_DATA_MAX_INDEX,
                      word_break_pages_part_1,
                      word_break_pages_part_2,
                      word_break_data);
}

bool
uc_isaletterornumeric(uchar c)
{
        switch (s_word_break(c)) {
        case UNICODE_WORD_BREAK_ALETTER:
        case UNICODE_WORD_BREAK_NUMERIC:
                return true;
        default:
                return false;
        }
}

#define ROW(other, cr, lf, newline, aletter, numeric, katakana, extendnumlet, \
            regional_indicator, midletter, midnumlet, midnum, format, extend) \
        { [UNICODE_WORD_BREAK_OTHER] = other, \
          [UNICODE_WORD_BREAK_CR] = cr, \
          [UNICODE_WORD_BREAK_LF] = lf, \
          [UNICODE_WORD_BREAK_NEWLINE] = newline, \
          [UNICODE_WORD_BREAK_FORMAT] = format, \
          [UNICODE_WORD_BREAK_EXTEND] = extend, \
          [UNICODE_WORD_BREAK_ALETTER] = aletter, \
          [UNICODE_WORD_BREAK_NUMERIC] = numeric, \
          [UNICODE_WORD_BREAK_KATAKANA] = katakana, \
          [UNICODE_WORD_BREAK_EXTENDNUMLET] = extendnumlet, \
          [UNICODE_WORD_BREAK_REGIONAL_INDICATOR] = regional_indicator, \
          [UNICODE_WORD_BREAK_MIDLETTER] = midletter, \
          [UNICODE_WORD_BREAK_MIDNUMLET] = midnumlet, \
          [UNICODE_WORD_BREAK_MIDNUM] = midnum }
#define K(s) (s | (1 << 4))
#define S(s) (s | (2 << 4))
#define D(s) (s | (3 << 4))
static const uint8_t wb_dfa[][UNICODE_WORD_BREAK_REGIONAL_INDICATOR + 1] = {
        ROW(0,1,  2 ,2,  3 ,  4 ,  5 ,  6 ,  7 ,  0 ,  0 ,  0 ,K(0),K(0)), // Other
        ROW(0,1,K(2),2,  3 ,  4 ,  5 ,  6 ,  7 ,  0 ,  0 ,  0 ,  0 ,  0 ), // CR
        ROW(0,1,  2 ,2,  3 ,  4 ,  5 ,  6 ,  7 ,  0 ,  0 ,  0 ,  0 ,  0 ), // LF | Newline
        ROW(0,1,  2 ,2,K(3),K(4),  5 ,K(6),  7 ,S(8),S(8),  0 ,K(3),K(3)), // ALetter
        ROW(0,1,  2 ,2,K(3),K(4),  5 ,K(6),  7 ,  0 ,S(9),S(9),K(4),K(4)), // Numeric
        ROW(0,1,  2 ,2,  3 ,  4 ,K(5),K(6),  7 ,  0 ,  0 ,  0 ,K(5),K(5)), // Katakana
        ROW(0,1,  2 ,2,K(3),K(4),K(5),K(6),  7 ,  0 ,  0 ,  0 ,K(6),K(6)), // ExtendNumLet
        ROW(0,1,  2 ,2,  3 ,  4 ,  5 ,  6 ,K(7),  0 ,  0 ,  0 ,K(7),K(7)), // Regional_Indicator
        ROW(0,1,  2 ,2,D(3),  4 ,  5 ,  6 ,  7 ,  0 ,  0 ,  0 ,K(8),K(8)), // ALetter (MidLetter | MidNumLet)
        ROW(0,1,  2 ,2,  3 ,D(4),  5 ,  6 ,  7 ,  0 ,  0 ,  0 ,K(9),K(9)), // Numeric (MidNum | MidNumLet)
};
#undef D
#undef S
#undef K

void
u_word_breaks(const char *string, size_t n, bool *breaks)
{
        const char *p = string;
        const char *end = p + n;
        bool *q = breaks;
        bool *s = NULL;
        uint8_t state = 2;
        while (p < end) {
                size_t l;
                state = wb_dfa[state & 0xf][s_word_break(u_lref(p, &l))];
                switch (state >> 4) {
                case 1:
                        *q = false;
                        break;
                case 2:
                        s = q;
                        *q = true;
                        break;
                case 3:
                        *s = false;
                        *q = false;
                        break;
                default:
                        *q = true;
                }
                p += l;
                q += l;
        }
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
