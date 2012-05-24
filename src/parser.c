#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>

#include "grammar.h"
#include "parser.h"

int nmc_grammar_parse(struct nmc_parser *parser);

static void
skip(struct nmc_parser *parser, xmlChar c)
{
        while (*parser->p == c)
                parser->p++;
}

xmlDocPtr
nmc_parse(const xmlChar *input)
{
        struct nmc_parser parser;
        parser.input = input;
        parser.p = parser.input;
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.doc = xmlNewDoc(BAD_CAST "1.0");

        skip(&parser, ' ');

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
substring(struct nmc_parser *parser, YYSTYPE *value, const xmlChar *end, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - parser->p;
        return token(parser, end, type);
}

static int
short_substring(struct nmc_parser *parser, YYSTYPE *value, const xmlChar *end, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - parser->p - 1;
        return token(parser, end, type);
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

static int
bol(struct nmc_parser *parser, YYSTYPE *value)
{
        parser->bol = false;

        int length;

        /* TODO Turn this into a state machine instead, so that we never
         * examine a byte more than once. */
        if (xmlStrncmp(parser->p, BAD_CAST "  ", 2) == 0)
                return token(parser, parser->p + 2, PARAGRAPH);
        else if (xmlStrncmp(parser->p, BAD_CAST "§ ", xmlUTF8Size(BAD_CAST "§ ")) == 0)
                return token(parser, parser->p + xmlUTF8Size(BAD_CAST "§ "), SECTION);
        else if (xmlStrncmp(parser->p, BAD_CAST "• ", xmlUTF8Size(BAD_CAST "• ")) == 0)
                return token(parser, parser->p + xmlUTF8Size(BAD_CAST "• "), ITEMIZATION);
        else if ((length = subscript(parser)) > 0 && *(parser->p + length) == ' ')
                return short_substring(parser, value, parser->p + length + 1, ENUMERATION);
        else if ((length = superscript(parser)) > 0 && *(parser->p + length) == ' ')
                return short_substring(parser, value, parser->p + length + 1, FOOTNOTE);
        else if (*parser->p == '\0')
                return END;
        else
                return token(parser, parser->p, ERROR);
}

static int
eol(struct nmc_parser *parser, YYSTYPE *value)
{
        const xmlChar *end = parser->p;
        end++;
        parser->p = end;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                parser->p = end;
                while (*end == ' ' || *end == '\n') {
                        if (*end == '\n')
                                parser->p = end + 1;
                        end++;
                }
                int spaces = end - parser->p;
                if (parser->want == INDENT && spaces > parser->indent + 2) {
                        parser->want = ERROR;
                        parser->indent += 2;
                        return token(parser, parser->p + parser->indent, INDENT);
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        parser->want = ERROR;
                        return dedents(parser, end, spaces);
                } else {
                        parser->want = ERROR;
                        return token(parser, parser->p + parser->indent, BLOCKSEPARATOR);
                }
        } else {
                int spaces = end - parser->p;
                if (spaces > parser->indent) {
                        return token(parser, end, CONTINUATION);
                } else if (spaces < parser->indent) {
                        if (spaces % 2 != 0)
                                return token(parser, end, CONTINUATION);
                        parser->bol = true;
                        return dedents(parser, end, spaces);
                } else {
                        parser->p = end;
                        return bol(parser, value);
                }
        }
}

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value)
{
        if (parser->dedents > 0)
                return dedent(parser, parser->p);

        if (parser->bol)
                return bol(parser, value);

        skip(parser, ' ');

        if (*parser->p == '\n')
                return eol(parser, value);

        const xmlChar *end = parser->p;
        while (*end != '\0' && *end != ' ' && *end != '\n')
                end++;
        if (parser->p == end)
                return END;

        return substring(parser, value, end, WORD);
}
