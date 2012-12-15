#include <config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmc/list.h>

#include <private.h>

#include "buffer.h"
#include "ext.h"
#include "grammar.h"
#include "parser.h"
#include "unicode.h"

struct nmc_error nmc_oom_error = {
        NULL,
        { 0, 0, 0, 0 },
        (char *)"memory exhausted"
};

void
nmc_error_free(struct nmc_error *error)
{
        list_for_each_safe(struct nmc_error, p, n, error) {
                if (p != &nmc_oom_error) {
                        free(p->message);
                        free(p);
                }
        }
}

struct nmc_error *
nmc_error_newv(YYLTYPE *location, const char *message, va_list args)
{
        struct nmc_error *error = malloc(sizeof(struct nmc_error));
        if (error == NULL)
                return NULL;
        error->next = NULL;
        error->location = *location;
        if (nmc_vasprintf(&error->message, message, args) == -1) {
                free(error);
                return NULL;
        }
        return error;
}

struct nmc_error *
nmc_error_new(YYLTYPE *location, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        struct nmc_error *error = nmc_error_newv(location, message, args);
        va_end(args);
        return error;
}

struct nmc_error *
nmc_error_newu(const char *message, ...)
{
        va_list args;
        va_start(args, message);
        struct nmc_error *error = nmc_error_newv(&(YYLTYPE){ 0, 0, 0, 0 }, message, args);
        va_end(args);
        return error;
}

static inline bool
parser_is_oom(struct parser *parser)
{
        return parser->errors.last == &nmc_oom_error;
}

void
parser_errors(struct parser *parser,
              struct nmc_error *first, struct nmc_error *last)
{
        if (parser_is_oom(parser)) {
                nmc_error_free(first);
                return;
        }
        if (parser->errors.first == NULL)
                parser->errors.first = first;
        if (parser->errors.last != NULL)
                parser->errors.last->next = first;
        parser->errors.last = last;
}

bool
parser_error(struct parser *parser, YYLTYPE *location, const char *message, ...)
{
        if (parser_is_oom(parser))
                return false;
        va_list args;
        va_start(args, message);
        struct nmc_error *error = nmc_error_newv(location, message, args);
        va_end(args);
        if (error == NULL) {
                parser_oom(parser);
                return false;
        }
        parser_errors(parser, error, error);
        return true;
}

void
parser_oom(struct parser *parser)
{
        if (parser_is_oom(parser))
                return;
        parser_errors(parser, &nmc_oom_error, &nmc_oom_error);
}

struct node *
nmc_parse(const char *input, struct nmc_error **errors)
{
        struct parser parser;
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
locate(struct parser *parser, YYLTYPE *location, int last_column)
{
        parser->location.last_column = last_column;
        *location = parser->location;
}

static int
token(struct parser *parser, YYLTYPE *location, const char *end, int type)
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
substring(struct parser *parser, YYLTYPE *location, YYSTYPE *value,
          const char *end, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - value->substring.string;
        return token(parser, location, end, type);
}

static int
dedent(struct parser *parser, YYLTYPE *location, const char *end)
{
        /* TODO: assert(parser->dedents > 0); */
        parser->dedents--;
        return token(parser, location, end, DEDENT);
}

static int
dedents(struct parser *parser, const char *begin, size_t spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        /* TODO: assert(parser->indent >= 0); */
        return dedent(parser, NULL, begin + parser->indent);
}

typedef bool (*isfn)(uchar);

static inline size_t
length_of_run(struct parser *parser, isfn is)
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
superscript(struct parser *parser)
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
subscript(struct parser *parser)
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
buffer(struct parser *parser, YYLTYPE *location, const char *begin)
{
        while (*begin == ' ')
                begin++;
        const char *end = begin;
        struct buffer b = BUFFER_INIT;

again:
        switch (*end) {
        case '\0':
                break;
        case '\n': {
                const char *send = end + 1;
                while (*send == ' ')
                        send++;
                if (!is_end(send) &&
                    (size_t)(send - (end + 1)) >= parser->indent + 2) {
                        if (!buffer_append(&b, begin, end - begin))
                                goto oom;
                        begin = end + parser->indent + 2;
                        end = send;
                        parser->location.last_line++;
                        goto again;
                }
                break;
        }
        case ' ':
                end++;
                if (!buffer_append(&b, begin, end - begin))
                        goto oom;
                while (*end == ' ')
                        end++;
                begin = end;
                goto again;
        default:
                end++;
                goto again;
        }
        if (!buffer_append(&b, begin, end - begin))
                goto oom;

        // NOTE We use a throwaway type here; caller must return actual type.
        token(parser, location, end, ERROR);
        return buffer_str(&b);
oom:
        token(parser, location, end, ERROR);
        free(b.content);
        return NULL;
}

static size_t
bol_space(struct parser *parser, size_t offset)
{
        if (*(parser->p + offset) == ' ')
                return 1;
        parser_error(parser, &parser->location,
                     "missing ‘ ’ after ‘%.*s’ at beginning of line",
                     (int)(u_next(parser->p) - parser->p), parser->p);
        return 0;
}

static int
footnote(struct parser *parser, YYLTYPE *location, YYSTYPE *value, size_t length)
{
        const char *id = parser->p;
        char *content = buffer(parser, location,
                               parser->p + length + bol_space(parser, length));
        if (content == NULL) {
                value->footnote = NULL;
                return FOOTNOTE;
        }
        struct nmc_error *error = NULL;
        value->footnote = footnote_new(location, strxdup(id, length), content, &error);
        free(content);
        if (error != NULL)
                parser_errors(parser, error, error);
        return FOOTNOTE;
}

static int
codeblock(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 4;
        const char *end = begin;
        struct buffer b = BUFFER_INIT;

        while (*end != '\0') {
                while (!is_end(end))
                        end++;
                if (!buffer_append(&b, begin, end - begin))
                        goto oom;
                if (*end != '\n')
                        break;
                size_t lines = 1;
                const char *sbegin = end + 1;
                const char *send = sbegin;
                while (*send == ' ' || *send == '\n') {
                        if (*send == '\n') {
                                lines++;
                                sbegin = send + 1;
                        }
                        send++;
                }
                if ((size_t)(send - sbegin) < parser->indent + 4)
                        break;
                if (!buffer_append_c(&b, '\n', lines))
                        goto oom;
                begin = sbegin + parser->indent + 4;
                end = send;
                parser->location.last_line += lines;
        }

        value->node = text_node_new(NODE_CODEBLOCK, buffer_str(&b));
        goto done;
oom:
        value->node = NULL;
done:
        return token(parser, location, end, CODEBLOCK);
}

static int
definition(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
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
                                value->node = text_node_new_dup(NODE_TERM, begin, send - begin);
                                return token(parser, location, end + 1, TERM);
                        }
                }
                end++;
        }

        // TODO Do this after location has been updated?
        parser_error(parser, &parser->location,
                     "missing ending “. /” for term in definition");
        return token(parser, location, end, AGAIN);
}

static int
bol_token(struct parser *parser, YYLTYPE *location, size_t length, int type)
{
        return token(parser,
                     location,
                     parser->p + length + bol_space(parser, length),
                     type);
}

#define U_PILCROW_SIGN ((uchar)0x00b6)
#define U_SECTION_SIGN ((uchar)0x00a7)
#define U_BULLET ((uchar)0x2022)
#define U_EM_DASH ((uchar)0x2014)

static bool
is_bol_symbol(const char *end)
{
        uchar c = u_dref(end);
        switch (c) {
        case U_PILCROW_SIGN:
        case U_SECTION_SIGN:
        case U_BULLET:
        case U_EM_DASH:
        case '/':
        case '>':
        case '|':
                return true;
        default:
                return is_subscript(c) || is_superscript(c);
        }
}

static int
bol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
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
        case U_PILCROW_SIGN:
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
                parser_error(parser, &parser->location,
                             "broken UTF-8 sequence at beginning of line starting with %#02x",
                             *parser->p);
        else if (!uc_issolid(c))
                parser_error(parser, &parser->location,
                             "unrecognized character U+%04X at beginning of line",
                             c);
        else
                parser_error(parser, &parser->location,
                             "unrecognized character ‘%.*s’ (U+%04X) at beginning of line",
                             (int)length, parser->p, c);

        return PARAGRAPH;
}

static int
eol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->location.last_line++;
        parser->location.first_line = parser->location.last_line;
        parser->location.first_column = 1;

        const char *begin = parser->p + 1, *end = begin;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                parser->location.last_line++;
                begin = end;
                while (true) {
                        while (*end == ' ')
                                end++;
                        if (*end != '\n')
                                break;
                        end++;
                        parser->location.last_line++;
                        begin = end;
                }
                size_t spaces = end - begin;
                if ((parser->want == INDENT && spaces >= parser->indent + 2) ||
                    (parser->want == ITEMINDENT && spaces >= parser->indent + 2 &&
                     (spaces > parser->indent + 2 || is_bol_symbol(end)))) {
                        int want = parser->want;
                        parser->want = ERROR;
                        parser->indent += 2;
                        locate(parser, location, parser->indent);
                        return token(parser, NULL, begin + parser->indent, want);
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        parser->want = ERROR;
                        locate(parser, location, spaces);
                        return dedents(parser, begin, spaces);
                } else if (parser->indent > 0 && spaces == parser->indent &&
                           !is_bol_symbol(end)) {
                        // EXAMPLE An itemization containing multiple
                        // paragraphs followed by a paragraph.
                        parser->want = ERROR;
                        parser->location.first_line = parser->location.last_line;
                        locate(parser, location, spaces + 1);
                        return dedents(parser, begin, spaces - 2);
                } else {
                        parser->want = ERROR;
                        parser->location.first_line = parser->location.last_line;
                        locate(parser, location, parser->indent + 1);
                        parser->p = begin + parser->indent;
                        return bol(parser, location, value);
                }
        } else {
                size_t spaces = end - begin;
                if (parser->want == ITEMINDENT && spaces == parser->indent + 2) {
                        parser->bol = true;
                        parser->want = ERROR;
                        parser->indent += 2;
                        locate(parser, location, parser->indent);
                        return token(parser, NULL, begin + parser->indent, ITEMINDENT);
                } else if (spaces > parser->indent) {
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
                        return dedents(parser, begin, spaces);
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
code(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 3;
        const char *end = begin;
        size_t length;
        while (!is_end(end) &&
               u_lref(end, &length) != U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                end += length;
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "missing ending ‘›’ for code inline")) {
                        value->node = NULL;
                        goto oom;
                }
        } else {
                while (u_dref(end) == U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                        end += length;
                send = end - length;
        }
        value->node = text_node_new_dup(NODE_CODE, begin, send - begin);
oom:
        return token(parser, location, end, CODE);
}

static int
emphasis(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 1;
        const char *end = begin;
        while (!is_end(end) &&
               (*end != '/' || !is_inline_end(end + 1)))
                end++;
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "missing ending ‘/’ for emphasis inline")) {
                        value->node = NULL;
                        goto oom;
                }
        } else
                end++;
        value->node = text_node_new_dup(NODE_EMPHASIS, begin, send - begin);
oom:
        return token(parser, location, end, EMPHASIS);
}

#define U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x2039)
#define U_SUPERSCRIPT_PLUS_SIGN ((uchar)0x207a)

int
parser_lex(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
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
                return token(parser, location, parser->p + length, ANCHORSEPARATOR);
        }

        if (is_group_end(end))
                return token(parser, location, parser->p + 1, ENDGROUP);
        else if ((length = superscript(parser)) > 0) {
                // TODO Only catch this if followed by is_inline_end().
                const char *begin = parser->p;
                int r = token(parser, location, parser->p + length, ANCHOR);
                value->node = anchor_node_new(location, begin, length);
                return r;
        }

        while (!is_inline_end(end))
                end++;
        if (end == parser->p)
                return token(parser, location, parser->p, END);

        return substring(parser, location, value, end, WORD);
}
