#ifndef SRC_PARSER_H
#define SRC_PARSER_H

struct nmc_parser
{
        const char *p;
        YYLTYPE location;
        size_t indent;
        size_t dedents;
        bool bol;
        int want;
        struct node *doc;
        struct anchors *anchors;
        struct {
                struct nmc_parser_error *first;
                struct nmc_parser_error *last;
        } errors;
};

void nmc_parser_errors(struct nmc_parser *parser,
                       struct nmc_parser_error *first,
                       struct nmc_parser_error *last);
void nmc_parser_error(struct nmc_parser *parser,
                      YYLTYPE *location, const char *message, ...) PRINTF(3, 4);

int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);

#endif
