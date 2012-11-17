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
        struct node *doc;
        xmlHashTablePtr anchors;
        xmlListPtr errors;
};

struct node *nmc_parse(const xmlChar *input, xmlListPtr *errors);
int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);

struct nmc_parser_error
{
        YYLTYPE location;
        char *message;
};

void nmc_parser_errorv(struct nmc_parser *parser, YYLTYPE *location,
                       const char *message, va_list args);
void nmc_parser_error(struct nmc_parser *parser, YYLTYPE *location,
                      const char *message, ...) PRINTF(3, 4);

#endif
