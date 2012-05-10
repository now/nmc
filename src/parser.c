#include <config.h>

#include <libxml/tree.h>

#include "grammar.h"
#include "parser.h"

int nmc_grammar_parse(struct nmc_parser *parser);

xmlDocPtr
nmc_parser_parse(const char *input)
{
        struct nmc_parser parser;
        parser.input = input;
        parser.p = parser.input;
        parser.doc = xmlNewDoc("1.0");

        nmc_grammar_parse(&parser);

        return parser.doc;
}

xmlDocPtr
nmc_parse(const char *input)
{
        return nmc_parser_parse(input);
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

xmlNodePtr
nmc_parser_create_title(struct nmc_parser *parser, const char *title)
{
        xmlNodePtr root = xmlNewNode(NULL, "nml");
        xmlDocSetRootElement(parser->doc, root);
        return xmlNewChild(root, NULL, "title", title);
}

