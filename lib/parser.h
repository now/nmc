struct nmc_parser {
        const char *p;
        YYLTYPE location;
        size_t indent;
        size_t dedents;
        bool bol;
        int want;
        struct node *doc;
        struct anchor *anchors;
        struct {
                struct nmc_error *first;
                struct nmc_error *last;
        } errors;
};

struct nmc_error *nmc_error_newv(struct nmc_location *location,
                                 const char *message,
                                 va_list args);
struct nmc_error *nmc_error_new(struct nmc_location *location,
                                const char *message,
                                ...) NMC_PRINTF(2, 3);
void nmc_parser_errors(struct nmc_parser *parser,
                       struct nmc_error *first,
                       struct nmc_error *last);
void nmc_parser_error(struct nmc_parser *parser,
                      YYLTYPE *location,
                      const char *message, ...) NMC_PRINTF(3, 4);

int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);
