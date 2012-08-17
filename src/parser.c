#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>
#include <string.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"

int nmc_grammar_parse(struct nmc_parser *parser);

static void
error_free(xmlLinkPtr link)
{
        xmlFree(xmlLinkGetData(link));
}

void
nmc_parser_errorv(struct nmc_parser *parser, YYLTYPE *location,
                  const char *message, va_list args)
{
        va_list saved;

        va_copy(saved, args);

        xmlChar buf[1];
        int size = xmlStrVPrintf(buf, sizeof(buf), (const xmlChar *)message, args);

        struct nmc_parser_error *error =
                (struct nmc_parser_error *)xmlMalloc(sizeof(struct nmc_parser_error));
        error->location = *location;
        error->message = (char *)xmlMalloc(size + 1);
        xmlStrVPrintf((xmlChar *)error->message, size + 1, (const xmlChar *)message, saved);
        va_end(saved);

        xmlListPushBack(parser->errors, error);
}

void
nmc_parser_error(struct nmc_parser *parser, YYLTYPE *location,
                 const char *message, ...)
{
        va_list args;
        va_start(args, message);
        nmc_parser_errorv(parser, location, message, args);
        va_end(args);
}


xmlDocPtr
nmc_parse(const xmlChar *input, xmlListPtr *errors)
{
        struct nmc_parser parser;
        parser.p = input;
        parser.location = (YYLTYPE){ 1, 1, 1, 1 };
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.want = TITLE;
        parser.doc = xmlNewDoc(BAD_CAST "1.0");
        parser.anchors = xmlHashCreate(16);
        parser.errors = *errors = xmlListCreate(error_free, NULL);

        nmc_grammar_parse(&parser);

        xmlHashFree(parser.anchors, (xmlHashDeallocator)xmlListDelete);

        return parser.doc;
}

static void
locate(struct nmc_parser *parser, YYLTYPE *location, int last_column)
{
        parser->location.last_column = last_column;
        *location = parser->location;
}

static int
token(struct nmc_parser *parser, YYLTYPE *location, const xmlChar *end, int type)
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
                  const xmlChar *end, int left, int right, int type)
{
        value->substring.string = parser->p + left;
        value->substring.length = end - value->substring.string - right;
        return token(parser, location, end, type);
}

static int
substring(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value,
          const xmlChar *end, int type)
{
        return trimmed_substring(parser, location, value, end, 0, 0, type);
}

static int
error(struct nmc_parser *parser, YYLTYPE *location)
{
        return token(parser, location, parser->p, ERROR);
}

static int
dedent(struct nmc_parser *parser, YYLTYPE *location, const xmlChar *end)
{
        /* TODO: assert(parser->dedents > 0); */
        parser->dedents--;
        return token(parser, location, end, DEDENT);
}

static int
dedents(struct nmc_parser *parser, YYLTYPE *location, const xmlChar *end, int spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        /* TODO: assert(parser->indent >= 0); */
        return dedent(parser, location, end);
}

static int
length_of_superscript(const xmlChar *input)
{
        switch (*input) {
        case 0xc2:
                switch (*(input + 1)) {
                case 0xb9: /* ¹ */
                case 0xb2: /* ² */
                case 0xb3: /* ³ */
                        return 2;
                default:
                        return 0;
                }
        case 0xe2:
                if (*(input + 1) == 0x81 &&
                    (*(input + 2) == 0xb0 || /* ⁰ */
                     (0xb4 <= *(input + 2) && *(input + 2) <= 0xb9))) /* ⁴⁻⁹ */
                        return 3;
        default:
                return 0;
        }
}

static int
superscript(struct nmc_parser *parser)
{
        int total = 0, length;
        while ((length = length_of_superscript(parser->p + total)) > 0)
                total += length;
        return total;
}

static int
length_of_subscript(const xmlChar *input)
{
        if (*input == 0xe2 && *(input + 1) == 0x82 &&
            (0x80 <= *(input + 2) && *(input + 2) <= 0x89))
                return 3;
        return 0;
}

static int
subscript(struct nmc_parser *parser)
{
        int total = 0, length;
        while ((length = length_of_subscript(parser->p + total)) > 0)
                total += 3;
        return total;
}

static inline bool
is_end(const xmlChar *end)
{
        return *end == '\0' || *end == '\n';
}

static inline bool
is_space_or_end(const xmlChar *end)
{
        return is_end(end) || *end == ' ';
}

static int
buffer(struct nmc_parser *parser, YYLTYPE *location, xmlBufferPtr buffer, const xmlChar *begin, int type)
{
        while (*begin == ' ')
                begin++;

        const xmlChar *end = begin;

again:
        switch (*end) {
        case '\0':
                break;
        case '\n': {
                const xmlChar *send = end + 1;
                while (*send == ' ')
                        send++;
                int spaces = send - (end + 1);
                if (!is_end(send) && spaces >= parser->indent + 2) {
                        xmlBufferAdd(buffer, begin, end - begin);
                        begin = end + parser->indent + 2;
                        end = send;
                        parser->location.last_line++;
                        goto again;
                }
                break;
        }
        case ' ':
                end++;
                xmlBufferAdd(buffer, begin, end - begin);
                while (*end == ' ')
                        end++;
                begin = end;
                goto again;
        default:
                end++;
                goto again;
        }
        xmlBufferAdd(buffer, begin, end - begin);

        return token(parser, location, end, type);
}

static int
footnote(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value, int length)
{
        if (*(parser->p + length) != ' ')
                return error(parser, location);
        value->raw_footnote.id = xmlStrndup(parser->p, length);
        value->raw_footnote.buffer = xmlBufferCreate();

        return buffer(parser, location, value->raw_footnote.buffer, parser->p + length + 1, FOOTNOTE);
}

static int
codeblock(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const xmlChar *begin = parser->p + 4;
        const xmlChar *end = begin;
        value->buffer = xmlBufferCreate();

again:
        while (!is_end(end))
                end++;
        if (*end == '\n') {
                int lines = 1;
                const xmlChar *bss = end + 1;
                const xmlChar *bse = bss;
                while (*bse == ' ' || *bse == '\n') {
                        if (*bse == '\n') {
                                lines++;
                                bss = bse + 1;
                        }
                        bse++;
                }
                int spaces = bse - bss;
                if (spaces >= parser->indent + 4) {
                        xmlBufferAdd(value->buffer, begin, end - begin);
                        for (int i = 0; i < lines; i++)
                                xmlBufferWriteChar(value->buffer, "\n");
                        begin = bss + parser->indent + 4;
                        end = bse;
                        parser->location.last_line += lines;
                        goto again;
                }
        }
        xmlBufferAdd(value->buffer, begin, end - begin);

        return token(parser, location, end, CODEBLOCK);
}

static int
definition(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const xmlChar *end = parser->p + 3;

        while (!is_end(end)) {
                if (*end == '.') {
                        const xmlChar *send = end;
                        end++;
                        while (*end == ' ')
                                end++;
                        if (*end == '/' && is_space_or_end(end + 1))
                                return trimmed_substring(parser,
                                                         location,
                                                         value,
                                                         end + 1,
                                                         2,
                                                         end - send + 1,
                                                         DEFINITION);
                }
                end++;
        }

        nmc_parser_error(parser, &parser->location,
                         "missing ending “. /” for term in definition");
        return token(parser, location, end, ERROR);
}

static int
bol_token(struct nmc_parser *parser, YYLTYPE *location, int length, int type)
{
        return *(parser->p + length) == ' ' ?
                token(parser, location, parser->p + length + 1, type) :
                error(parser, location);
}

static int
bol(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->bol = false;

        int length;
        if ((length = subscript(parser)) > 0)
                return bol_token(parser, location, length, ENUMERATION);
        else if ((length = superscript(parser)) > 0)
                return footnote(parser, location, value, length);

        switch (*parser->p) {
        case ' ':
                if (*(parser->p + 1) == ' ') {
                        if (*(parser->p + 2) == ' ' && *(parser->p + 3) == ' ')
                                return codeblock(parser, location, value);
                        return token(parser, location, parser->p + 2, PARAGRAPH);
                }
                break;
        case 0xc2:
                if (*(parser->p + 1) == 0xa7)
                        return bol_token(parser, location, 2, SECTION);
                break;
        case 0xe2:
                if (*(parser->p + 1) == 0x80)
                        switch (*(parser->p + 2)) {
                        case 0xa2:
                                return bol_token(parser, location, 3, ITEMIZATION);
                        case 0x94:
                                return bol_token(parser, location, 3, ATTRIBUTION);
                        }
                break;
        case '/':
                if (*(parser->p + 1) == ' ')
                        return definition(parser, location, value);
                break;
        case '>':
                return bol_token(parser, location, 1, QUOTE);
        case '|':
                switch (*(parser->p + 1)) {
                case ' ':
                        return token(parser, location, parser->p + 2, ROW);
                case '-': {
                        const xmlChar *end = parser->p + 3;
                        while (!is_end(end))
                                end++;
                        return token(parser, location, end, TABLESEPARATOR);
                }
                }
                break;
        case '\0':
                return token(parser, location, parser->p, END);
        }

        length = 0;
        const xmlChar *end = parser->p;
        while (*end != '\0' && length < 7)
                end++, length++;
        int uc = xmlGetUTF8Char(parser->p, &length);
        if (uc == -1)
                nmc_parser_error(parser, &parser->location,
                                 "broken UTF-8 sequence at beginning of line starting with %#02x",
                                 *parser->p);
        else
                nmc_parser_error(parser, &parser->location,
                                 "unrecognized character ‘%.*s’ (U+%04X) at beginning of line",
                                 length, parser->p, uc);

        return PARAGRAPH;
}

static int
eol(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const xmlChar *end = parser->p;
        end++;
        parser->location.last_line++;
        parser->location.first_line = parser->location.last_line;
        parser->location.first_column = 1;
        const xmlChar *begin = end;
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
                int spaces = end - begin;
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
                int spaces = end - begin;
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

static bool
is_reference_or_space_or_end(const xmlChar *end)
{
        return is_space_or_end(end) || length_of_superscript(end) > 0;
}

static bool
is_group_end(const xmlChar *end)
{
        if (*end == '}') {
                do {
                        end++;
                } while (*end == '}');
                return is_reference_or_space_or_end(end);
        }

        return false;
}

static bool
is_inline_end(const xmlChar *end)
{
        return is_reference_or_space_or_end(end) ||
                is_group_end(end);
}

static int
code(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const xmlChar *end = parser->p + 3;
        while (!is_end(end) &&
               !(*end == 0xe2 && *(end + 1) == 0x80 && *(end + 2) == 0xba))
                end++;
        if (is_end(end)) {
                nmc_parser_error(parser, &parser->location,
                                 "missing ending ‘›’ for code inline");
                return trimmed_substring(parser, location, value, end, 3, 0, CODE);
        }
        while (*end == 0xe2 && *(end + 1) == 0x80 && *(end + 2) == 0xba)
                end += 3;
        return trimmed_substring(parser, location, value, end, 3, 3, CODE);
}

static int
emphasis(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const xmlChar *end = parser->p + 1;
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

int
nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        if (parser->dedents > 0)
                return dedent(parser, location, parser->p);

        if (parser->bol)
                return bol(parser, location, value);

        int length;
        const xmlChar *end = parser->p;

        if (parser->want == TITLE) {
                parser->want = ERROR;
                value->buffer = xmlBufferCreate();
                return buffer(parser, location, value->buffer, end, TITLE);
        }

        switch (*end) {
        case ' ':
                do {
                        end++;
                } while (*end == ' ');
                return substring(parser, location, value, end, SPACE);
        case '\n':
                return eol(parser, location, value);
        case '|':
                return token(parser, location, parser->p + 1, ENTRY);
        case '/':
                return emphasis(parser, location, value);
        case '{':
                return token(parser, location, parser->p + 1, BEGINGROUP);
        case 0xe2:
                end++;
                if (*end == 0x80 && *(end + 1) == 0xb9)
                        return code(parser, location, value);
                else if (*end == 0x81 && *(end + 1) == 0xba)
                        // TODO Only catch this if followed by is_inline_end()
                        // (or perhaps only if followed by superscript()).
                        return token(parser, location, parser->p + 3, SIGILSEPARATOR);
                goto word;
        }

        if (is_group_end(end))
                return token(parser, location, parser->p + 1, ENDGROUP);
        else if ((length = superscript(parser)) > 0)
                // TODO Only catch this if followed by is_inline_end().
                return substring(parser, location, value, parser->p + length, SIGIL);

word:
        while (!is_inline_end(end))
                end++;
        if (end == parser->p)
                return token(parser, location, parser->p, END);

        return substring(parser, location, value, end, WORD);
}
