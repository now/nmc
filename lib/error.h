extern struct nmc_error nmc_oom_error;

struct nmc_error *nmc_error_newv(struct nmc_location *location,
                                 const char *message,
                                 va_list args);
struct nmc_error *nmc_error_new(struct nmc_location *location,
                                const char *message,
                                ...) NMC_PRINTF(2, 3);
struct nmc_error *nmc_error_newu(const char *message, ...) NMC_PRINTF(1, 2);
