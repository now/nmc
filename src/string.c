#include <libxml/xmlmemory.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "list.h"
#include "nmc.h"
#include "string.h"

struct nmc_string
{
        size_t allocated;
        size_t length;
        char *content;
};

struct nmc_string *
nmc_string_new(const char *string, size_t length)
{
        struct nmc_string *n = nmc_new(struct nmc_string);
        n->length = n->allocated = 0;
        n->content = NULL;
        return nmc_string_append(n, string, length);
}

struct nmc_string *
nmc_string_new_empty(void)
{
        return nmc_string_new(NULL, 0);
}

void
nmc_string_free(struct nmc_string *text)
{
        nmc_free(text->content);
        nmc_free(text);
}

struct nmc_string *
nmc_string_append(struct nmc_string *text, const char *string, size_t length)
{
        if (string == NULL || length == 0)
                return text;

        size_t needed = text->length + length + 1;
        if (needed > text->allocated) {
                /* TODO libxml2 uses 4096 as default size.  We might want
                   something slightly larger here. */
                size_t n = text->allocated > 0 ? text->allocated * 2 : needed + 32;
                while (n < needed) {
                        if (n > SIZE_MAX / 2)
                                /* TODO Report error */
                                return text;
                        n *= 2;
                }
                char *t = realloc(text->content, n);
                if (t == NULL)
                        /* TODO Report error */
                        return text;
                text->content = t;
                text->allocated = n;
        }

        memcpy(text->content + text->length, string, length);
        text->length += length;

        return text;
}

char *
nmc_string_str(struct nmc_string *text)
{
        text->content[text->length] = '\0';
        return text->content;
}

char *
nmc_string_str_free(struct nmc_string *text)
{
        char *s = nmc_string_str(text);
        size_t l = text->length;
        nmc_free(text);
        char *t = realloc(s, l + 1);
        return t == NULL ? s : t;
}
