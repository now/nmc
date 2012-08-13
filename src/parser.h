#ifndef SRC_PARSER_H
#define SRC_PARSER_H

struct nmc_parser
{
        const xmlChar *p;
        YYLTYPE location;
        int indent;
        int dedents;
        bool bol;
        int want;
        bool words;
        xmlDocPtr doc;
};

xmlDocPtr nmc_parse(const xmlChar *input);
int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);

#endif
