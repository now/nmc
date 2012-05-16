#ifndef SRC_PARSER_H
#define SRC_PARSER_H

enum nmc_parser_state
{
        BEGINNING_OF_LINE,
        AFTER_INDENT,
        OTHER
};


struct nmc_parser
{
        const xmlChar *input;
        const xmlChar *p;
        int indent;
        int dedents;
        enum nmc_parser_state state;
        xmlDocPtr doc;
};

xmlDocPtr nmc_parse(const xmlChar *input);
int nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value);

#endif
