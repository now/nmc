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

int
nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value)
{
        const xmlChar *begin = parser->p;

        while (*begin == ' ')
                begin++;

        const xmlChar *end = begin;

        while (*end != '\0' && *end != ' ')
                end++;

        if (begin == end)
                return 0;

        parser->p = end;

        value->substring.string = begin;
        value->substring.length = end - begin;
        return WORD;
}
