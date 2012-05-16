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
        parser.state = OTHER;
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

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value)
{
        const xmlChar *end;

bol:
        end = parser->p;

        if (parser->state == BEGINNING_OF_LINE) {
                while (*end == ' ')
                        end++;
                if (*end == '\n')
                        return substring(parser, value, end + 1, BLANKLINE);
                parser->state = AFTER_INDENT;
                int spaces = end - parser->p;
                if (spaces == parser->indent + 2) {
                        end -= 2;
                        /* paragraph */
                } else if (spaces == parser->indent + 4) {
                        parser->indent += 2;
                        parser->p = end - 2;
                        return INDENT;
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        /* TODO: Calculate this. */
                        while (parser->indent != spaces) {
                                parser->indent -= 2;
                                parser->dedents++;
                        }
                        parser->p = end;
                } else if (end != parser->p)
                        return token(parser, end, ERROR);
        }

        if (parser->dedents > 0) {
                parser->dedents--;
                return DEDENT;
        }

        if (parser->state == AFTER_INDENT) {
                parser->state = OTHER;
                if (xmlStrncmp(end, BAD_CAST "  ", 2) == 0)
                        return token(parser, end + 2, PARAGRAPH);
                else if (xmlStrncmp(end, BAD_CAST "§ ", xmlUTF8Size(BAD_CAST "§ ")) == 0)
                        return token(parser, end + xmlUTF8Size(BAD_CAST "§ "), SECTION);
                else if (parser->p == end)
                        return END;
                else
                        return token(parser, end, ERROR);
        }

        skip(parser, ' ');
        end = parser->p;

        if (*end == '\n') {
                parser->p = end + 1;
                parser->state = BEGINNING_OF_LINE;
                goto bol;
        }

        while (*end != '\0' && *end != ' ' && *end != '\n')
                end++;
        if (parser->p == end)
                return END;

        return substring(parser, value, end, WORD);
}
