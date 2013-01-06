#include <config.h>

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>

static char *
upcase(char *string)
{
        for (char *p = string; *p != '\0'; p++)
                *p = toupper(*p);
        return string;
}

#define UNICODE_LAST_CHAR 0x10ffff
struct range {
        uint32_t start;
        uint32_t end;
        char *script;
};
static struct range data[UNICODE_LAST_CHAR + 1];

static int
scriptcmp(const void *a, const void *b)
{
        return strcmp(((const struct range *)a)->script, ((const struct range *)b)->script);
}

static int
startcmp(const void *a, const void *b)
{
        return ((const struct range *)a)->start - ((const struct range *)b)->start;
}

#define EASY_MAX 0x2000

int
main(void)
{
        char line[1024];
        int n = 0;
        while ((fgets(line, sizeof(line), stdin)) != NULL) {
                char *p = line;
                if (*p == '#' || *p == '\n')
                        continue;
                char *end;
                data[n].start = (uint32_t)strtol(strsep(&p, ";"), &end, 16);
                data[n].end = *end == '.' ? (uint32_t)strtol(end + 2, NULL, 16) : data[n].start;
                while (*p == ' ')
                        p++;
                data[n].script = upcase(strdup(strsep(&p, " ")));
                n++;
        }
        qsort(data, n, sizeof(data[0]), scriptcmp);
        puts("enum unicode_script {\n"
             "\tUNICODE_SCRIPT_UNKNOWN,");
        for (int i = 0; i < n; i++) {
                printf("\tUNICODE_SCRIPT_%s,\n", data[i].script);
                while (i + 1 < n && strcmp(data[i].script, data[i + 1].script) == 0)
                        i++;
        }
        qsort(data, n, sizeof(data[0]), startcmp);
        printf("};\n\n#define UNICODE_SCRIPT_EASY_TABLE_MAX_INDEX %#04x\n\n"
               "static const uint8_t script_easy_table[] = {", EASY_MAX);
        int i = 0, start = -1, end = -1;
        const char *script = NULL;
        for (int c = 0; c < EASY_MAX; c++) {
                if (c % 2 == 0)
                        fputs("\n\t", stdout);
                else
                        fputs(" ", stdout);
                if (c > end) {
                        start = data[i].start;
                        end = data[i].end;
                        script = data[i].script;
                        i++;
                }
                printf("UNICODE_SCRIPT_%s,", c < start ? "UNKNOWN" : script);
        }
        if (end >= EASY_MAX) {
                i--;
                data[i].start = EASY_MAX;
        }
        puts("\n};\n\nstatic const struct {\n"
             "\tuchar start;\n"
             "\tuint16_t chars;\n"
             "\tuint16_t script;\n"
              "} script_table[] = {");
        while (i < n) {
                int j = i;
                while (j <= n - 1 &&
                       data[j + 1].start == data[j].end + 1 &&
                       strcmp(data[j + 1].script, data[j].script) == 0)
                        j++;
                printf("\t{ %#7x, %5d, UNICODE_SCRIPT_%s },\n",
                       data[i].start, data[j].end - data[i].start + 1, data[i].script);
                i = j + 1;
        }
        puts("};");

        return EXIT_SUCCESS;
}
