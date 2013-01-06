#define UNICODE_LAST_CHAR 0x10ffff

static int
datanamecmp(const void *a, const void *b)
{
        return strcmp((const char *)a, ((struct dataname *)b)->u);
}

static const char *
data_name(const char *u)
{
        struct dataname *name = bsearch(u, names, lengthof(names), sizeof(names[0]), datanamecmp);

        if (name == NULL) {
                fprintf(stderr, "unknown Unicode " DATA_NAME " name: %s\n", u);
                exit(EXIT_FAILURE);
        }

        return name->c;
}

struct page {
        const char *data[256];
        bool homogenous;
};

static const char *data[UNICODE_LAST_CHAR + 1];
static struct page pages[lengthof(data) / 256];

#define UNICODE_FIRST_CHAR_PART_2 0xe0000

static uint8_t
part(int part, int first, int last, uint8_t j)
{
        printf("\n// U+%04X through U+%04X\nstatic const uint8_t " DATA_NAME "_pages_part_%d[] = {\n",
               first, last, part);
        for (int i = first; i <= last; i += 256) {
                struct page *page = &pages[i / 256];
                if (page->homogenous)
                        printf("\tUNICODE_%s + UNICODE_" DATA_NAME_UPPERCASE "_DATA_MAX_INDEX,\n",
                               page->data[0]);
                else {
                        printf("\t%u, // page %d\n", j, i / 256);
                        j++;
                }
        }
        puts("};");
        return j;
}

static void
parts(int last_char_part_1)
{
        uint8_t max = 0;
        for (int i = 0; i < (int)lengthof(pages); i++) {
                pages[i].homogenous = true;
                for (int j = 0, k = i * 256; j < 256; j++, k++) {
                        pages[i].data[j] = data[k];
                        if (pages[i].data[j] != pages[i].data[0])
                                pages[i].homogenous = false;
                }
                if (!pages[i].homogenous) {
                        if (lengthof(names) + max + 1 > UINT8_MAX) {
                                fprintf(stderr,
                                        "uint8_t not big enough to fit %u pages with %zu names",
                                        max + 1, lengthof(names));
                                exit(EXIT_FAILURE);
                        }
                        max++;
                }
        }
        puts("enum unicode_" DATA_NAME " {");
        for (int i = 0; i < (int)lengthof(names); i++)
                printf("\tUNICODE_%s,\n", names[i].c);
        puts("};\nstatic const int8_t " DATA_NAME "_data[][256] = {");
        for (int i = 0, l = 0; i < (int)lengthof(pages); i++)
                if (!pages[i].homogenous) {
                        printf("\t{ // page %d, index %d\n", i, l);
                        for (int j = 0; j < 256; j += 2)
                                printf("\t\tUNICODE_%s, UNICODE_%s,\n",
                                       pages[i].data[j], pages[i].data[j + 1]);
                        puts("\t},");
                        l++;
                }
        printf("};\n\n#define UNICODE_LAST_CHAR_" DATA_NAME_UPPERCASE "_PART_1 ((uchar)%#04x)\n"
               "#define UNICODE_" DATA_NAME_UPPERCASE "_DATA_MAX_INDEX ((uint8_t)%u)\n",
               last_char_part_1, max);
        part(2, UNICODE_FIRST_CHAR_PART_2, UNICODE_LAST_CHAR, part(1, 0, last_char_part_1, 0));
}
