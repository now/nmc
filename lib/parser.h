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

extern struct nmc_error nmc_oom_error;
struct nmc_error *nmc_error_newv(struct nmc_location *location,
                                 const char *message,
                                 va_list args);
struct nmc_error *nmc_error_new(struct nmc_location *location,
                                const char *message,
                                ...) NMC_PRINTF(2, 3);
struct nmc_error *nmc_error_newu(const char *message, ...) NMC_PRINTF(1, 2);
void nmc_parser_errors(struct nmc_parser *parser,
                       struct nmc_error *first,
                       struct nmc_error *last);
bool nmc_parser_error(struct nmc_parser *parser,
                      YYLTYPE *location,
                      const char *message, ...) NMC_PRINTF(3, 4);
void nmc_parser_oom(struct nmc_parser *parser);

int nmc_parser_lex(struct nmc_parser *parser, YYLTYPE *location, YYSTYPE *value);
