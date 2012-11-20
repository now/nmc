#ifndef SRC_PARSER_H
#define SRC_PARSER_H

struct nmc_parser_error
{
        struct nmc_parser_error *next;
        YYLTYPE location;
        char *message;
};

struct nmc_parser_error *nmc_parser_error_newv(YYLTYPE *location,
                                               const char *message,
                                               va_list args);
struct nmc_parser_error *nmc_parser_error_new(YYLTYPE *location,
                                              const char *message,
                                              ...) PRINTF(2, 3);
void nmc_parser_error_free(struct nmc_parser_error *error);

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

struct node *nmc_parse(const xmlChar *input, struct nmc_parser_error **errors);
int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);

#endif
