#ifndef SRC_PARSER_H
#define SRC_PARSER_H

struct nmc_parser
{
        const xmlChar *input;
        const xmlChar *p;
        int indent;
        int dedents;
        bool bol;
        int want;
        xmlDocPtr doc;
};

xmlDocPtr nmc_parse(const xmlChar *input);
int nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value);

#endif
