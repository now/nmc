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
        if (parser->p != parser->input)
                return 0;

        value->string = parser->input;
        parser->p += xmlStrlen(parser->input);
        return TITLE;
}
