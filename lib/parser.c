#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

#include <nmc.h>
#include <nmc/list.h>

#include "buffer.h"
#include "ext.h"
#include "grammar.h"
#include "parser.h"
#include "unicode.h"

void
nmc_parser_error_free(struct nmc_parser_error *error)
{
        list_for_each_safe(struct nmc_parser_error, p, n, error) {
                nmc_free(p->message);
                nmc_free(p);
        }
}

struct nmc_parser_error *
nmc_parser_error_newv(YYLTYPE *location, const char *message, va_list args)
{
        struct nmc_parser_error *error = nmc_new(struct nmc_parser_error);
        error->next = NULL;
        error->location = *location;
        nmc_vasprintf(&error->message, message, args);
        return error;
}

struct nmc_parser_error *
nmc_parser_error_new(YYLTYPE *location, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        struct nmc_parser_error *error = nmc_parser_error_newv(location, message, args);
        va_end(args);
        return error;
}

void
nmc_parser_errors(struct nmc_parser *parser,
                  struct nmc_parser_error *first, struct nmc_parser_error *last)
{
        if (parser->errors.first == NULL)
                parser->errors.first = first;
        if (parser->errors.last != NULL)
                parser->errors.last->next = first;
        parser->errors.last = last;
}

void
nmc_parser_error(struct nmc_parser *parser, YYLTYPE *location, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        struct nmc_parser_error *error = nmc_parser_error_newv(location, message, args);
        va_end(args);
        nmc_parser_errors(parser, error, error);
}

struct node *
nmc_parse(const char *input, struct nmc_parser_error **errors)
{
        struct nmc_parser parser;
        parser.p = input;
        parser.location = (YYLTYPE){ 1, 1, 1, 1 };
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.want = TITLE;
        parser.doc = NULL;
        parser.anchors = NULL;
        parser.errors.first = parser.errors.last = NULL;

        nmc_grammar_parse(&parser);

        anchor_free(parser.anchors);

        *errors = parser.errors.first;
        return parser.doc;
}

static void
locate(struct nmc_parser *parser, YYLTYPE *location, int last_column)
{
        parser->location.last_column = last_column;
        *location = parser->location;
}

static int
token(struct nmc_parser *parser, YYLTYPE *location, const char *end, int type)
{
        if (location != NULL)
                locate(parser,
                       location,
                       parser->location.first_column +
                       (end - parser->p - (end == parser->p ? 0 : 1)));

        parser->location.first_line = parser->location.last_line;
        parser->location.last_column++;
        parser->location.first_column = parser->location.last_column;
        parser->p = end;
        return type;
}

static int
trimmed_substring(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value,
                  const char *end, size_t left, size_t right, int type)
{
        value->substring.string = parser->p + left;
        value->substring.length = end - value->substring.string - right;
        return token(parser, location, end, type);
}

static int
substring(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value,
          const char *end, int type)
{
        return trimmed_substring(parser, location, value, end, 0, 0, type);
}

static int
dedent(struct nmc_parser *parser, YYLTYPE *location, const char *end)
{
        /* TODO: assert(parser->dedents > 0); */
        parser->dedents--;
        return token(parser, location, end, DEDENT);
}

static int
dedents(struct nmc_parser *parser, YYLTYPE *location, const char *end, size_t spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        /* TODO: assert(parser->indent >= 0); */
        return dedent(parser, location, end);
}

typedef bool (*isfn)(uchar);

static inline size_t
length_of_run(struct nmc_parser *parser, isfn is)
{
        const char *end = parser->p;
        while (is(u_dref(end)))
                end = u_next(end);
        return end - parser->p;
}

#define U_SUPERSCRIPT_0 ((uchar)0x2070)
#define U_SUPERSCRIPT_1 ((uchar)0x00b9)
#define U_SUPERSCRIPT_2 ((uchar)0x00b2)
#define U_SUPERSCRIPT_3 ((uchar)0x00b3)
#define U_SUPERSCRIPT_4 ((uchar)0x2074)
#define U_SUPERSCRIPT_9 ((uchar)0x2079)

static inline bool
is_superscript(uchar c)
{
        switch (c) {
        case U_SUPERSCRIPT_0:
        case U_SUPERSCRIPT_1:
        case U_SUPERSCRIPT_2:
        case U_SUPERSCRIPT_3:
                return true;
        default:
                return U_SUPERSCRIPT_4 <= c && c <= U_SUPERSCRIPT_9;
        }
}

static inline size_t
superscript(struct nmc_parser *parser)
{
        return length_of_run(parser, is_superscript);
}

#define U_SUBSCRIPT_0 ((uchar)0x2080)
#define U_SUBSCRIPT_9 ((uchar)0x2089)

static inline bool
is_subscript(uchar c)
{
        return U_SUBSCRIPT_0 <= c && c <= U_SUBSCRIPT_9;
}

static inline size_t
subscript(struct nmc_parser *parser)
{
        return length_of_run(parser, is_subscript);
}

static inline bool
is_end(const char *end)
{
        return *end == '\0' || *end == '\n';
}

static inline bool
is_space_or_end(const char *end)
{
        return is_end(end) || *end == ' ';
}

static char *
buffer(struct nmc_parser *parser, YYLTYPE *location, const char *begin)
{
        while (*begin == ' ')
                begin++;

        struct buffer *b = buffer_new_empty();
        const char *end = begin;

again:
        switch (*end) {
        case '\0':
                break;
        case '\n': {
                const char *send = end + 1;
                while (*send == ' ')
                        send++;
                size_t spaces = send - (end + 1);
                if (!is_end(send) && spaces >= parser->indent + 2) {
                        buffer_append(b, begin, end - begin);
                        begin = end + parser->indent + 2;
                        end = send;
                        parser->location.last_line++;
                        goto again;
                }
                break;
        }
        case ' ':
                end++;
                buffer_append(b, begin, end - begin);
                while (*end == ' ')
                        end++;
                begin = end;
                goto again;
        default:
                end++;
                goto again;
        }
        buffer_append(b, begin, end - begin);

        // NOTE We use a throwaway type here; caller must return actual type.
        token(parser, location, end, ERROR);

        return buffer_str_free(b);
}

static size_t
bol_space(struct nmc_parser *parser, size_t offset)
{
        if (*(parser->p + offset) == ' ')
                return 1;
        nmc_parser_error(parser, &parser->location,
                         "missing ‘ ’ after ‘%.*s’ at beginning of line",
                         (int)(u_next(parser->p) - parser->p), parser->p);
        return 0;
}

static int
footnote(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value, size_t length)
{
        char *id = strndup(parser->p, length);
        struct nmc_parser_error *error = NULL;
        value->footnote = footnote_new(location, id,
                                       buffer(parser, location,
                                              parser->p + length + bol_space(parser, length)),
                                       &error);
        if (error != NULL)
                nmc_parser_errors(parser, error, error);
        return FOOTNOTE;
}

static int
codeblock(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 4;
        const char *end = begin;
        struct buffer *b = buffer_new_empty();

again:
        while (!is_end(end))
                end++;
        if (*end == '\n') {
                size_t lines = 1;
                const char *bss = end + 1;
                const char *bse = bss;
                while (*bse == ' ' || *bse == '\n') {
                        if (*bse == '\n') {
                                lines++;
                                bss = bse + 1;
                        }
                        bse++;
                }
                size_t spaces = bse - bss;
                if (spaces >= parser->indent + 4) {
                        buffer_append(b, begin, end - begin);
                        for (size_t i = 0; i < lines; i++)
                                buffer_append(b, "\n", 1);
                        begin = bss + parser->indent + 4;
                        end = bse;
                        parser->location.last_line += lines;
                        goto again;
                }
        }
        buffer_append(b, begin, end - begin);

        value->node = text_node_new(NODE_CODEBLOCK, buffer_str_free(b));
        return token(parser, location, end, CODEBLOCK);
}

static int
definition(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 1 + bol_space(parser, 1);
        const char *end = begin;

        while (!is_end(end)) {
                if (*end == '.') {
                        const char *send = end;
                        end++;
                        while (*end == ' ')
                                end++;
                        if (*end == '/' && is_space_or_end(end + 1)) {
                                value->node = text_node_new(NODE_TERM, strndup(begin, send - begin));
                                return token(parser, location, end + 1, TERM);
                        }
                }
                end++;
        }

        nmc_parser_error(parser, &parser->location,
                         "missing ending “. /” for term in definition");
        return token(parser, location, end, AGAIN);
}

static int
bol_token(struct nmc_parser *parser, YYLTYPE *location, size_t length, int type)
{
        return token(parser,
                     location,
                     parser->p + length + bol_space(parser, length),
                     type);
}

#define U_SECTION_SIGN ((uchar)0x00a7)
#define U_BULLET ((uchar)0x2022)
#define U_EM_DASH ((uchar)0x2014)

static int
bol(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->bol = false;

        size_t length;
        if ((length = subscript(parser)) > 0)
                return bol_token(parser, location, length, ENUMERATION);
        else if ((length = superscript(parser)) > 0)
                return footnote(parser, location, value, length);

        uchar c = u_lref(parser->p, &length);
        switch (c) {
        case ' ':
                if (*(parser->p + 1) == ' ') {
                        if (*(parser->p + 2) == ' ' && *(parser->p + 3) == ' ')
                                return codeblock(parser, location, value);
                        return token(parser, location, parser->p + 2, PARAGRAPH);
                }
                return bol_token(parser, location, length, PARAGRAPH);
        case U_SECTION_SIGN:
                return bol_token(parser, location, length, SECTION);
        case U_BULLET:
                return bol_token(parser, location, length, ITEMIZATION);
        case U_EM_DASH:
                return bol_token(parser, location, length, ATTRIBUTION);
        case '/':
                return definition(parser, location, value);
        case '>':
                return bol_token(parser, location, length, QUOTE);
        case '|':
                if (*(parser->p + 1) == '-') {
                        // TODO This should probably be + 2.
                        const char *end = parser->p + 3;
                        while (!is_end(end))
                                end++;
                        return token(parser, location, end, TABLESEPARATOR);
                }
                return bol_token(parser, location, length, ROW);
        case '\0':
                return token(parser, location, parser->p, END);
        }

        if (c == U_BAD_INPUT_CHAR)
                nmc_parser_error(parser, &parser->location,
                                 "broken UTF-8 sequence at beginning of line starting with %#02x",
                                 *parser->p);
        else if (!uc_issolid(c))
                nmc_parser_error(parser, &parser->location,
                                 "unrecognized character U+%04X at beginning of line",
                                 c);
        else
                nmc_parser_error(parser, &parser->location,
                                 "unrecognized character ‘%.*s’ (U+%04X) at beginning of line",
                                 (int)length, parser->p, c);

        return PARAGRAPH;
}

static int
eol(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *end = parser->p;
        end++;
        parser->location.last_line++;
        parser->location.first_line = parser->location.last_line;
        parser->location.first_column = 1;
        const char *begin = end;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                parser->location.last_line++;
                begin = end;
                while (*end == ' ' || *end == '\n') {
                        if (*end == '\n') {
                                begin = end + 1;
                                parser->location.last_line++;
                        }
                        end++;
                }
                size_t spaces = end - begin;
                if (parser->want == INDENT && spaces > parser->indent + 2) {
                        parser->want = ERROR;
                        parser->indent += 2;
                        locate(parser, location, parser->indent);
                        return token(parser, NULL, begin + parser->indent, INDENT);
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        parser->want = ERROR;
                        locate(parser, location, spaces);
                        return dedents(parser, NULL, end, spaces);
                } else {
                        parser->want = ERROR;
                        locate(parser, location, parser->indent);
                        return substring(parser, NULL, value, begin + parser->indent, BLOCKSEPARATOR);
                }
        } else {
                size_t spaces = end - begin;
                if (spaces > parser->indent) {
                        locate(parser, location, parser->indent + 1);
                        parser->location.last_column--;
                        return token(parser, NULL, begin + parser->indent, CONTINUATION);
                } else if (spaces < parser->indent) {
                        locate(parser, location, spaces + 1);
                        parser->location.last_column--;
                        /* TODO Why’s this a continuation? */
                        if (spaces % 2 != 0)
                                return token(parser, NULL, end, CONTINUATION);
                        parser->bol = true;
                        return dedents(parser, NULL, end, spaces);
                } else {
                       parser->p = end;
                       return bol(parser, location, value);
                }
        }
}

static inline bool
is_reference_or_space_or_end(const char *end)
{
        return is_space_or_end(end) || is_superscript(u_dref(end));
}

static inline bool
is_group_end(const char *end)
{
        if (*end == '}') {
                do {
                        end++;
                } while (*end == '}');
                return is_reference_or_space_or_end(end);
        }

        return false;
}

static inline bool
is_inline_end(const char *end)
{
        return is_reference_or_space_or_end(end) ||
                is_group_end(end);
}

#define U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x203a)

static int
code(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 3;
        const char *end = begin;
        size_t length;
        while (!is_end(end) &&
               u_lref(end, &length) != U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                end += length;
        const char *send = end;
        if (is_end(end)) {
                nmc_parser_error(parser, &parser->location,
                                 "missing ending ‘›’ for code inline");
        } else {
                while (u_dref(end) == U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                        end += length;
                send = end - length;
        }
        value->node = text_node_new(NODE_CODE, strndup(begin, send - begin));
        return token(parser, location, end, CODE);
}

static int
emphasis(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *end = parser->p + 1;
        while (!is_end(end) &&
               (*end != '/' || !is_inline_end(end + 1)))
                end++;
        if (is_end(end)) {
                nmc_parser_error(parser, &parser->location,
                                 "missing ending ‘/’ for emphasis inline");
                return trimmed_substring(parser, location, value, end, 1, 0, EMPHASIS);
        }
        end++;
        return trimmed_substring(parser, location, value, end, 1, 1, EMPHASIS);
}

#define U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x2039)
#define U_SUPERSCRIPT_PLUS_SIGN ((uchar)0x207a)

int
nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        if (parser->dedents > 0)
                return dedent(parser, location, parser->p);

        if (parser->bol)
                return bol(parser, location, value);

        const char *end = parser->p;

        if (parser->want == TITLE) {
                parser->want = ERROR;
                value->node = text_node_new(NODE_TEXT, buffer(parser, location, end));
                return TITLE;
        }

        size_t length;
        uchar c = u_lref(end, &length);
        switch (c) {
        case ' ':
                do {
                        end++;
                } while (*end == ' ');
                return substring(parser, location, value, end, SPACE);
        case '\n':
                return eol(parser, location, value);
        case '|':
                return token(parser, location, parser->p + length, ENTRYSEPARATOR);
        case '/':
                return emphasis(parser, location, value);
        case '{':
                return token(parser, location, parser->p + length, BEGINGROUP);
        case U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK:
                return code(parser, location, value);
        case U_SUPERSCRIPT_PLUS_SIGN:
                // TODO Only catch this if followed by is_inline_end()
                // (or perhaps only if followed by superscript()).
                return token(parser, location, parser->p + length, SIGILSEPARATOR);
        }

        if (is_group_end(end))
                return token(parser, location, parser->p + 1, ENDGROUP);
        else if ((length = superscript(parser)) > 0)
                // TODO Only catch this if followed by is_inline_end().
                return substring(parser, location, value, parser->p + length, SIGIL);

        while (!is_inline_end(end))
                end++;
        if (end == parser->p)
                return token(parser, location, parser->p, END);

        return substring(parser, location, value, end, WORD);
}
