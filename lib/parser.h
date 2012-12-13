struct parser {
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
void parser_errors(struct parser *parser,
                   struct nmc_error *first,
                   struct nmc_error *last);
bool parser_error(struct parser *parser,
                  YYLTYPE *location,
                  const char *message, ...) NMC_PRINTF(3, 4);
void parser_oom(struct parser *parser);

int parser_lex(struct parser *parser, YYLTYPE *location, YYSTYPE *value);
