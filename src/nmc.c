#include <config.h>

#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nmc.h>
#include <nmc/list.h>

#include <buffer.h>

struct nmc_option {
        char c;
        const char *name;
        int has_arg;
        const char *argument;
        const char *help;
} options[] = {
        { 'h', "help", no_argument, NULL, "Display this help" },
        { 'v', "version", no_argument, NULL, "Display version string" },
        { '\0', NULL, no_argument, NULL, NULL }
};
#define options_for_each(item) \
        for (struct nmc_option *item = options; item->c != '\0'; item++)

static int
report_nmc_error(const struct nmc_error *error)
{
        char *s = nmc_location_str(&error->location);
        fprintf(stderr, "%s: %s\n", s, error->message);
        free(s);

        return 1;
}

static void
usage(void)
{
        fprintf(stdout,
                "Usage: %s [OPTION]...\n"
                "Process standard input as compact neat markup and turn it into its XML representation.\n"
                "\n"
                "Options:\n",
                PACKAGE_NAME);
        size_t longest = 0;
        options_for_each(p) {
                size_t length = p->argument == NULL ? 0 : strlen(p->argument);
                switch (p->has_arg) {
                case required_argument:
                        length += 1;
                        break;
                case optional_argument:
                        length += 3;
                        break;
                }
                if (length > longest)
                        longest = length;
        }
        options_for_each(p) {
                fprintf(stdout, "  -%c, --%s", p->c, p->name);
                switch (p->has_arg) {
                case required_argument:
                        fprintf(stdout, "=%-*s", (int)longest, p->argument);
                        break;
                case optional_argument:
                        fprintf(stdout, "[=%s]%*s", p->argument, (int)longest, " ");
                        break;
                }
                fprintf(stdout, "  %s\n", p->help);
        }
}

int
main(int argc, char *const *argv)
{
        size_t n = 0, length = 0;
        options_for_each(p) {
                n++;
                switch (p->has_arg) {
                case required_argument: length += 1; break;
                case optional_argument: length += 2; break;
                }
        }
        length += n;
        char shorts[length + 1];
        struct option longs[n + 1];
        char *q = shorts;
        struct option *r = longs;
        options_for_each(p) {
                *q++ = p->c;
                switch (p->has_arg) {
                case optional_argument:
                        *q++ = ':';
                case required_argument:
                        *q++ = ':';
                }
                r->name = p->name;
                r->has_arg = p->has_arg;
                r->flag = NULL;
                r->val = p->c;
                r++;
        }
        shorts[length] = '\0';
        memset(q, 0, sizeof(*q));

        opterr = 0;
        int index;
        switch (getopt_long(argc, argv, shorts, longs, &index)) {
        case 'h':
                usage();
                return EXIT_SUCCESS;
        case 'v':
                fprintf(stdout, "%s\n", PACKAGE_STRING);
                return EXIT_SUCCESS;
        case '?':
                if (optopt == 0) {
                        fprintf(stderr, "%s: unknown option: %s\n",
                                PACKAGE_NAME, argv[optind - 1]);
                        return EXIT_FAILURE;
                }
                options_for_each(p) {
                        if (p->c == optopt) {
                                fprintf(stderr,
                                        "%s: option --%s requires an argument\n",
                                        PACKAGE_NAME, p->name);
                                return EXIT_FAILURE;
                        }
                }
                fprintf(stderr, "%s: unknown option: -%c\n", PACKAGE_NAME, optopt);
                return EXIT_FAILURE;
        case -1:
                break;
        }
        if (optind != argc) {
                fprintf(stderr, "%s: unknown argument: %s\n",
                        PACKAGE_NAME, argv[optind]);
                return EXIT_FAILURE;
        }

        struct buffer b = BUFFER_INIT;
        if (!buffer_read(&b, STDIN_FILENO, 0)) {
                perror(PACKAGE_NAME ": error while reading from stdin");
                return EXIT_FAILURE;
        }
        char *buffer = buffer_str(&b);

        int result = EXIT_SUCCESS;

        if (getenv("NMC_DEBUG"))
                nmc_grammar_debug = 1;

        struct nmc_error *errors = NULL;
        struct node *doc = NULL;

        if (nmc_initialize(&errors))
                doc = nmc_parse(buffer, &errors);
        if (errors != NULL)
                result = EXIT_FAILURE;
        list_for_each(struct nmc_error, p, errors)
                report_nmc_error(p);
        nmc_error_free(errors);

        if (result == EXIT_SUCCESS)
                nmc_node_xml(doc);

        nmc_node_free(doc);

        nmc_finalize();

        free(buffer);

        return result;
}

