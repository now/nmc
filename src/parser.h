#ifndef SRC_PARSER_H
#define SRC_PARSER_H

struct nmc_parser
{
        const xmlChar *input;
        const xmlChar *p;
        xmlDocPtr doc;
};

xmlDocPtr nmc_parse(const char *input);
int nmc_parser_lex(struct nmc_parser *parser, YYSTYPE *value);
xmlNodePtr nmc_parser_create_title(struct nmc_parser *parser, const char *title);

#endif
