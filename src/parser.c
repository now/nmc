#include <config.h>

#include <libxml/tree.h>

#include "grammar.h"
#include "parser.h"

int nmc_grammar_parse(struct nmc_parser *parser);

xmlDocPtr
nmc_parse(const xmlChar *input)
{
        struct nmc_parser parser;
        parser.input = input;
        parser.p = parser.input;
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
substring(struct nmc_parser *parser, YYSTYPE *value, const xmlChar *end, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - parser->p;
        return token(parser, end, type);
}

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value)
{
        while (*parser->p == ' ')
                parser->p++;

        const xmlChar *end = parser->p;

        switch (*end) {
        case '\n':
                end++;
                while (*end == ' ')
                        end++;
                if (*end == '\n')
                        return substring(parser, value, end, BLANKLINE);
                else
                        return token(parser, end, PARAGRAPH);
        default:
                break;
        }

        while (*end != '\0' && *end != ' ' && *end != '\n')
                end++;
        if (parser->p == end)
                return END;

        return substring(parser, value, end, WORD);
}
