#ifndef SRC_STRING_H
#define SRC_STRING_H

struct nmc_string;

struct nmc_string *nmc_string_new(const char *string, size_t length);
struct nmc_string *nmc_string_new_empty(void);
void nmc_string_free(struct nmc_string *text);
struct nmc_string *nmc_string_append(struct nmc_string *text,
                                     const char *string, size_t length);
char *nmc_string_str(struct nmc_string *text);
char *nmc_string_str_free(struct nmc_string *text);

#endif SRC_STRING_H
