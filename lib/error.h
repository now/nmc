struct nmc_parser_error *nmc_parser_error_newv(struct nmc_location *location,
                                               const char *message,
                                               va_list args);
struct nmc_parser_error *nmc_parser_error_new(struct nmc_location *location,
                                              const char *message,
                                              ...) NMC_PRINTF(2, 3);

bool nmc_error_init(struct nmc_error *error, int number, const char *message);
bool nmc_error_oom(struct nmc_error *error);
bool nmc_error_dyninit(struct nmc_error *error, int number, char *message);
bool nmc_error_formatv(struct nmc_error *error, int number,
                       const char *message, va_list args);
bool nmc_error_format(struct nmc_error *error, int number, const char *message,
                      ...) NMC_PRINTF(3, 4);
