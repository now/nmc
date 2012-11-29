#include <config.h>

#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <private.h>
#include <nmc.h>
#include <nmc/list.h>

extern int nmc_grammar_debug;

struct {
        char c;
        const char *name;
        int has_arg;
        const char *argument;
        const char *help;
} options[] = {
        { 'h', "help", no_argument, NULL, "Display this help" },
        { 'v', "version", no_argument, NULL, "Display version string" }
};

static int
report_nmc_parser_error(const struct nmc_parser_error *error)
{
        char *s = nmc_location_str(&error->location);
        fprintf(stderr, "%s: %s\n", s, error->message);
        nmc_free(s);

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
        int longest = 0;
        for (size_t i = 0; i < nmc_lengthof(options); i++) {
                int length = options[i].argument == NULL ? 0 : strlen(options[i].argument);
                switch (options[i].has_arg) {
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
        for (size_t i = 0; i < nmc_lengthof(options); i++) {
                fprintf(stdout, "  -%c, --%s", options[i].c, options[i].name);
                switch (options[i].has_arg) {
                case required_argument:
                        fprintf(stdout, "=%-*s", longest, options[i].argument);
                        break;
                case optional_argument:
                        fprintf(stdout, "[=%s]%*s", options[i].argument, longest, " ");
                        break;
                }
                fprintf(stdout, "  %s\n", options[i].help);
        }
}

int
main(int argc, char *const *argv)
{
        int length = nmc_lengthof(options);
        for (size_t i = 0; i < nmc_lengthof(options); i++) {
                switch (options[i].has_arg) {
                case required_argument:
                        length += 1;
                        break;
                case optional_argument:
                        length += 2;
                        break;
                }
        }
        char shorts[length + 1];
        struct option longs[nmc_lengthof(options) + 1];
        for (size_t i = 0, j = 0; i < nmc_lengthof(options); i++, j++) {
                longs[i].name = options[i].name;
                longs[i].has_arg = options[i].has_arg;
                longs[i].flag = NULL;
                longs[i].val = shorts[j] = options[i].c;
                switch (options[i].has_arg) {
                case optional_argument:
                        j += 1;
                        shorts[j] = ':';
                case required_argument:
                        j += 1;
                        shorts[j] = ':';
                }
        }
        shorts[length] = '\0';
        memset(&longs[nmc_lengthof(options)], 0, sizeof(longs[0]));

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
                for (size_t i = 0; i < nmc_lengthof(options); i++) {
                        if (options[i].c == optopt) {
                                fprintf(stderr,
                                        "%s: option --%s requires an argument\n",
                                        PACKAGE_NAME, options[i].name);
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

        int result = EXIT_SUCCESS;

        char *buffer = nmc_new_n(char, 2000000);
        ssize_t bytes = read(STDIN_FILENO, buffer, 2000000);
        buffer[bytes] = '\0';

        if (getenv("NMC_DEBUG"))
                nmc_grammar_debug = 1;

        nmc_grammar_initialize();

        struct nmc_parser_error *errors;
        struct node *doc = nmc_parse(buffer, &errors);
        if (errors != NULL)
                result = EXIT_FAILURE;
        list_for_each(struct nmc_parser_error, p, errors)
                report_nmc_parser_error(p);
        nmc_parser_error_free(errors);

        if (result == EXIT_SUCCESS)
                nmc_node_to_xml(doc);

        node_free(doc);

        nmc_grammar_finalize();

        nmc_free(buffer);

        return result;
}

