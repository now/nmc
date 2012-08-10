#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>
#include <string.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"

int nmc_grammar_parse(struct nmc_parser *parser);

xmlDocPtr
nmc_parse(const xmlChar *input)
{
        struct nmc_parser parser;
        parser.input = input;
        parser.p = parser.input;
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.want = ERROR;
        parser.words = false;
        parser.doc = xmlNewDoc(BAD_CAST "1.0");

        nmc_grammar_parse(&parser);

        return parser.doc;
}

static int
token(struct nmc_parser *parser, const xmlChar *end, int type)
{
        parser->p = end;
        return type;
}

static int
short_substring(struct nmc_parser *parser, YYSTYPE *value, const xmlChar *end, int shorten, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - parser->p - shorten;
        return token(parser, end, type);
}

static int
substring(struct nmc_parser *parser, YYSTYPE *value, const xmlChar *end, int type)
{
        return short_substring(parser, value, end, 0, type);
}

/* TODO Remove end */
static int
dedent(struct nmc_parser *parser, const xmlChar *end)
{
        /* TODO: assert(parser->dedents > 0); */
        parser->dedents--;
        return token(parser, end, DEDENT);
}

static int
dedents(struct nmc_parser *parser, const xmlChar *end, int spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        /* TODO: assert(parser->indent >= 0); */
        return dedent(parser, end);
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
definition(struct nmc_parser *parser, YYSTYPE *value)
{
        parser->p += 2;
        const xmlChar *end = parser->p;

        while (!is_end(end)) {
                if (*end == '.') {
                        const xmlChar *send = end;
                        end++;
                        while (*end == ' ')
                                end++;
                        if (*end == '/' && is_space_or_end(end + 1))
                                return short_substring(parser,
                                                       value,
                                                       end + 1,
                                                       end - send + 1,
                                                       DEFINITION);
                }
                end++;
        }

        return token(parser, parser->p, ERROR);
}

static int
bol(struct nmc_parser *parser, YYSTYPE *value)
{
        parser->bol = false;

        int length;

        /* TODO Turn this into a state machine instead, so that we never
         * examine a byte more than once. */
        if (xmlStrncmp(parser->p, BAD_CAST "    ", 4) == 0)
                return token(parser, parser->p + 4, CODEBLOCK);
        else if (xmlStrncmp(parser->p, BAD_CAST "  ", 2) == 0)
                return token(parser, parser->p + 2, PARAGRAPH);
        else if (xmlStrncmp(parser->p, BAD_CAST "§ ", strlen("§ ")) == 0)
                return token(parser, parser->p + strlen("§ "), SECTION);
        else if (xmlStrncmp(parser->p, BAD_CAST "• ", strlen("• ")) == 0)
                return token(parser, parser->p + strlen("• "), ITEMIZATION);
        else if (xmlStrncmp(parser->p, BAD_CAST "/ ", strlen("/ ")) == 0)
                return definition(parser, value);
        else if ((length = subscript(parser)) > 0 && *(parser->p + length) == ' ')
                return short_substring(parser, value, parser->p + length + 1, 1, ENUMERATION);
        else if ((length = superscript(parser)) > 0 && *(parser->p + length) == ' ')
                return short_substring(parser, value, parser->p + length + 1, 1, FOOTNOTE);
        else if (xmlStrncmp(parser->p, BAD_CAST "> ", strlen("> ")) == 0)
                return token(parser, parser->p + strlen("> "), QUOTE);
        else if (xmlStrncmp(parser->p, BAD_CAST "— ", strlen("— ")) == 0)
                return token(parser, parser->p + strlen("— "), ATTRIBUTION);
        else if (xmlStrncmp(parser->p, BAD_CAST "| ", strlen("| ")) == 0)
                return token(parser, parser->p + strlen("| "), ROW);
        else if (xmlStrncmp(parser->p, BAD_CAST "|-", strlen("|-")) == 0) {
                const xmlChar *end = parser->p + strlen("|-");
                while (*end != '\n' && *end != '\0')
                        end++;
                return token(parser, end, TABLESEPARATOR);
        } else if (*parser->p == '\0')
                return END;
        else
                return token(parser, parser->p, ERROR);
}

static int
eol(struct nmc_parser *parser, YYSTYPE *value)
{
        const xmlChar *end = parser->p;
        end++;
        const xmlChar *begin = end;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                begin = end;
                while (*end == ' ' || *end == '\n') {
                        if (*end == '\n')
                                begin = end + 1;
                        end++;
                }
                int spaces = end - begin;
                if (parser->want == INDENT && spaces > parser->indent + 2) {
                        parser->want = ERROR;
                        parser->indent += 2;
                        return token(parser, begin + parser->indent, INDENT);
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        parser->want = ERROR;
                        return dedents(parser, end, spaces);
                } else {
                        parser->want = ERROR;
                        return substring(parser, value, begin + parser->indent, BLOCKSEPARATOR);
                }
        } else {
                int spaces = end - begin;
                if (spaces > parser->indent) {
                        return token(parser, begin + parser->indent, CONTINUATION);
                } else if (spaces < parser->indent) {
                        /* TODO Why’s this a continuation? */
                        if (spaces % 2 != 0)
                                return token(parser, end, CONTINUATION);
                        parser->bol = true;
                        return dedents(parser, end, spaces);
                } else {
                        /* TODO This needs to be checked when doing location calculations. */
                        parser->p = end;
                        return bol(parser, value);
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
code(struct nmc_parser *parser, YYSTYPE *value)
{
        parser->p += 3;
        const xmlChar *end = parser->p;
        while (!is_end(end) &&
               !(*end == 0xe2 && *(end + 1) == 0x80 && *(end + 2) == 0xba))
                end++;
        if (is_end(end))
                return token(parser, parser->p - 3, ERROR);
        while (*end == 0xe2 && *(end + 1) == 0x80 && *(end + 2) == 0xba)
                end += 3;
        return short_substring(parser, value, end, 3, CODE);
}

static int
emphasis(struct nmc_parser *parser, YYSTYPE *value)
{
        parser->p++;
        const xmlChar *end = parser->p;
        while (!is_end(end) &&
               (*end != '/' || !is_inline_end(end + 1)))
                end++;
        if (is_end(end))
                return token(parser, parser->p - 1, ERROR);
        end++;
        return short_substring(parser, value, end, 1, EMPHASIS);
}

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value, UNUSED(YYLTYPE *location))
{
        if (parser->dedents > 0)
                return dedent(parser, parser->p);

        if (parser->bol)
                return bol(parser, value);

        int length;
        const xmlChar *end = parser->p;

        if (*end == ' ') {
                do {
                        end++;
                } while (*end == ' ');
                return substring(parser, value, end, SPACE);
        } else if (*end == '\n') {
                return eol(parser, value);
        } else if (parser->want == WORD) {
                /* Fall through. */
        } else if (*end == 0xe2 && *(end + 1) == 0x80 && *(end + 2) == 0xb9) {
                parser->p = end;
                return code(parser, value);
        } else if (*end == '|') {
                return token(parser, parser->p + 1, ENTRY);
        } else if  (*end == '/') {
                return emphasis(parser, value);
        } else if (*end == '{') {
                return token(parser, parser->p + 1, BEGINGROUP);
        } else if (is_group_end(end)) {
                return token(parser, parser->p + 1, ENDGROUP);
        } else if ((length = superscript(parser)) > 0) {
                // TODO Only catch this if followed by is_inline_end().
                return substring(parser, value, parser->p + length, REFERENCE);
        } else if (*end == 0xe2 && *(end + 1) == 0x81 && *(end + 2) == 0xba) {
                // TODO Only catch this if followed by is_inline_end().
                return token(parser, parser->p + 3, REFERENCESEPARATOR);
        }

        while (!is_inline_end(end))
                end++;
        if (end == parser->p)
                return END;

        return substring(parser, value, end, WORD);
}
