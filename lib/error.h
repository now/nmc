struct nmc_error *nmc_error_newv(struct nmc_location *location,
                                 const char *message,
                                 va_list args);
struct nmc_error *nmc_error_new(struct nmc_location *location,
                                const char *message,
                                ...) NMC_PRINTF(2, 3);
