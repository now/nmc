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

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value)
{
        const xmlChar *end = parser->p;

        if (parser->dedents > 0)
                return dedent(parser, end);

bol:
        if (parser->bol) {
                /* TODO Make this into a function */
                parser->bol = false;
                if (xmlStrncmp(end, BAD_CAST "  ", 2) == 0)
                        return token(parser, end + 2, PARAGRAPH);
                else if (xmlStrncmp(end, BAD_CAST "§ ", xmlUTF8Size(BAD_CAST "§ ")) == 0)
                        return token(parser, end + xmlUTF8Size(BAD_CAST "§ "), SECTION);
                else if (xmlStrncmp(end, BAD_CAST "• ", xmlUTF8Size(BAD_CAST "• ")) == 0)
                        return token(parser, end + xmlUTF8Size(BAD_CAST "• "), ENUMERATION);
                else if (*end == '\0')
                        return END;
                else
                        return token(parser, end, ERROR);
        }

        skip(parser, ' ');
        end = parser->p;

        if (*end == '\n') {
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
                                return token(parser, parser->p, BLOCKSEPARATOR);
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
                                parser->bol = true;
                                parser->p = end;
                                goto bol;
                        }
                }
        }

        while (*end != '\0' && *end != ' ' && *end != '\n')
                end++;
        if (parser->p == end)
                return END;

        return substring(parser, value, end, WORD);
}
