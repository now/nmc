struct nmc_parser_error *nmc_parser_error_newv(struct nmc_location *location,
                                               const char *message,
                                               va_list args) NMC_PRINTF(2, 0);
struct nmc_parser_error *nmc_parser_error_new(struct nmc_location *location,
                                              const char *message,
                                              ...) NMC_PRINTF(2, 3);
