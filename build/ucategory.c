#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lengthof(a) (sizeof(a) / sizeof((a)[0]))

#define UNICODE_LAST_CHAR 0x10ffff
#define LAST " Last>"

struct categoryname {
        const char *category;
        const char *name;
};

static const struct categoryname names[] = {
        { "Cc", "OTHER_CONTROL" },
        { "Cf", "OTHER_FORMAT" },
        { "Cn", "OTHER_NOT_ASSIGNED" },
        { "Co", "OTHER_PRIVATE_USE" },
        { "Cs", "OTHER_SURROGATE" },
        { "Ll", "LETTER_LOWERCASE" },
        { "Lm", "LETTER_MODIFIER" },
        { "Lo", "LETTER_OTHER" },
        { "Lt", "LETTER_TITLECASE" },
        { "Lu", "LETTER_UPPERCASE" },
        { "Mc", "MARK_SPACING_COMBINING" },
        { "Me", "MARK_ENCLOSING" },
        { "Mn", "MARK_NONSPACING" },
        { "Nd", "NUMBER_DECIMAL_DIGIT" },
        { "Nl", "NUMBER_LETTER" },
        { "No", "NUMBER_OTHER" },
        { "Pc", "PUNCTUATION_CONNECTOR" },
        { "Pd", "PUNCTUATION_DASH" },
        { "Pe", "PUNCTUATION_CLOSE" },
        { "Pf", "PUNCTUATION_FINAL_QUOTE" },
        { "Pi", "PUNCTUATION_INITIAL_QUOTE" },
        { "Po", "PUNCTUATION_OTHER" },
        { "Ps", "PUNCTUATION_OPEN" },
        { "Sc", "SYMBOL_CURRENCY" },
        { "Sk", "SYMBOL_MODIFIER" },
        { "Sm", "SYMBOL_MATH" },
        { "So", "SYMBOL_OTHER" },
        { "Zl", "SEPARATOR_LINE" },
        { "Zp", "SEPARATOR_PARAGRAPH" },
        { "Zs", "SEPARATOR_SPACE" },
};

static int
categorynamecmp(const void *a, const void *b)
{
        return strcmp((const char *)a, ((struct categoryname *)b)->category);
}

static const char *
category_name(const char *category)
{
        struct categoryname *name = bsearch(category, names, lengthof(names), sizeof(names[0]), categorynamecmp);

        if (name == NULL) {
                fprintf(stderr, "unknown Unicode category: %s\n", category);
                exit(EXIT_FAILURE);
        }

        return name->name;
}

struct page {
        const char *data[256];
        bool homogenous;
};

static const char *categories[UNICODE_LAST_CHAR + 1];
static struct page pages[lengthof(categories) / 256];

static int16_t
part(int part, int first, int last, int16_t j)
{
        printf("\n// U+%04X through U+%04X\nstatic const int16_t category_pages_part_%d[] = {\n", first, last, part);
        for (int i = first; i <= last; i += 256) {
                struct page *page = &pages[i / 256];
                if (page->homogenous)
                        printf("\tUNICODE_%s + UNICODE_MAX_TABLE_INDEX,\n", page->data[0]);
                else {
                        printf("\t%d, // page %d\n", j, i / 256);
                        j++;
                }
        }
        puts("};");
        return j;
}

#define LAST_CHAR_PART_1 0xe0000

int
main(void)
{
        const char *cn = category_name("Cn");
        int last_char_part_1 = -1;
        char line[1024];
        int previous = -1;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                char *p = line;
                int c = (int)strtol(strsep(&p, ";"), NULL, 16);
                if (c >= LAST_CHAR_PART_1 && previous < LAST_CHAR_PART_1)
                        last_char_part_1 = ((previous >> 8) + 1) * 256 - 1;
                char *name = strsep(&p, ";");
                const char *category = category_name(strsep(&p, ";"));
                if (c > previous + 1 && strcmp(name + strlen(name) + 1 - sizeof(LAST), LAST) != 0) {
                        for (int i = previous + 1; i < c; i++)
                                categories[i] = cn;
                        categories[c] = category;
                } else
                        for (int i = previous + 1; i <= c; i++)
                                categories[i] = category;
                previous = c;
        }
        for (int i = previous + 1; i <= UNICODE_LAST_CHAR; i++)
                categories[i] = cn;
        for (int i = 0; i < (int)lengthof(pages); i++) {
                pages[i].homogenous = true;
                for (int j = 0, k = i * 256; j < 256; j++, k++) {
                        pages[i].data[j] = categories[k];
                        if (pages[i].data[j] != pages[i].data[0])
                                pages[i].homogenous = false;
                }
        }
        puts("enum category {");
        for (int i = 0; i < (int)lengthof(names); i++)
                printf("\tUNICODE_%s,\n", names[i].name);
        puts("};\n\nstatic const char category_data[][256] = {");
        for (int i = 0, l = 0; i < (int)lengthof(pages); i++)
                if (!pages[i].homogenous) {
                        printf("\t{ // page %d, index %d\n", i, l);
                        for (int j = 0; j < 256; j += 2)
                                printf("\t\tUNICODE_%s, UNICODE_%s,\n", pages[i].data[j], pages[i].data[j + 1]);
                        puts("\t},");
                        l++;
                }
        puts("};");
        printf("\n#define UNICODE_LAST_CHAR_PART_1 ((uchar)%#04x)\n", last_char_part_1);
        part(2, LAST_CHAR_PART_1, UNICODE_LAST_CHAR, part(1, 0, last_char_part_1, 0));
        return EXIT_SUCCESS;
}
