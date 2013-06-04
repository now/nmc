#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nmc.h>
#include <nmc/list.h>

#include <private.h>

#include <buffer.h>

struct nmc_option {
        char c;
        const char *name;
        int has_arg;
        const char *argument;
        const char *help;
} options[] = {
        { 'h', "help", no_argument, NULL, "Display this help" },
        { 'V', "version", no_argument, NULL, "Display version string" },
        { '\0', NULL, no_argument, NULL, NULL }
};
#define options_for_each(item) \
        for (struct nmc_option *item = options; item->c != '\0'; item++)

static bool
report_nmc_parser_error(const struct nmc_parser_error *error, const char *path)
{
        char *s = nmc_location_str(&error->location);
        bool r = (path == NULL ||
                  (fputs(path, stderr) != EOF && fputs(":", stderr) != EOF)) &&
                (s == NULL ||
                  (fputs(s, stderr) != EOF && fputs(": ", stderr) != EOF)) &&
                fputs(error->message, stderr) != EOF &&
                fputc('\n', stderr) != EOF;
        free(s);
        return r;
}

static bool
report_nmc_error(const struct nmc_error *error, const char *path)
{
        return fputs(PACKAGE_NAME, stderr) != EOF &&
                fputs(": ", stderr) != EOF &&
                (path == NULL ||
                 (fputs(path, stderr) != EOF && fputs(": ", stderr) != EOF)) &&
                fputs(error->message, stderr) != EOF &&
                (error->number < 0 ||
                 ((*error->message == '\0' || fputs(": ", stderr) != EOF) &&
                  fputs(strerror(error->number), stderr) != EOF)) &&
                fputc('\n', stderr) != EOF;
}

static void
usage(void)
{
        fprintf(stdout,
                "Usage: %s [OPTION]... [FILE]\n"
                "Process FILE or standard input as NoMarks text and turn it into NoMarks XML.\n"
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

static bool
convert(char *content, const char *path)
{
        struct nmc_parser_error *errors = NULL;
        struct nmc_node *doc = nmc_parse(content, &errors);
        free(content);
        if (doc == NULL) {
                list_for_each(struct nmc_parser_error, p, errors)
                        if (!report_nmc_parser_error(p, path))
                                break;
                nmc_parser_error_free(errors);
                return false;
        }

        struct nmc_fd_output fd;
        nmc_fd_output_init(&fd, STDOUT_FILENO);
        struct nmc_buffered_output output;
        nmc_buffered_output_init(&output, &fd.output);
        struct nmc_error error;
        bool r = nmc_node_xml(doc, &output.output, &error);
        struct nmc_error ignored;
        nmc_output_close(&output.output, r ? &error : &ignored);
        nmc_node_free(doc);
        if (!r)
                report_nmc_error(&error, path);
        return r;
}

static bool
read_fd(int fd, char **content, struct nmc_error *error)
{
        struct buffer b = BUFFER_INIT;
        if (!buffer_read(&b, fd, 0)) {
                free(b.content);
                return nmc_error_init(error, errno, "error reading from file");
        }
        *content = buffer_str(&b);
        return true;
}

static bool
convert_stdin(void)
{
        char *content;
        struct nmc_error error;
        if (!read_fd(STDIN_FILENO, &content, &error)) {
                report_nmc_error(&error, NULL);
                return false;
        }
        return convert(content, NULL);
}

static bool
read_path(const char *path, char **content, struct nmc_error *error)
{
        int fd = open(path, O_RDONLY);
        if (fd == -1)
                return nmc_error_init(error, errno, "error opening file");
        if (!read_fd(fd, content, error)) {
                close(fd);
                return false;
        }
        if (close(fd) == -1) {
                free(*content);
                return nmc_error_init(error, errno, "error closing file");
        }
        return true;
}

static bool
convert_path(const char *path)
{
        char *content;
        struct nmc_error error;
        if (!read_path(path, &content, &error)) {
                report_nmc_error(&error, path);
                return false;
        }
        return convert(content, path);
}

static size_t
short_length(void)
{
        size_t n = 0, length = 0;
        options_for_each(p) {
                n++;
                switch (p->has_arg) {
                case required_argument: length += 1; break;
                case optional_argument: length += 2; break;
                }
        }
        return length + n;
}

static void
args_fill(char *shorts, struct option *longs)
{
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
        *q = '\0';
        memset(r, 0, sizeof(*r));
}

int
main(int argc, char *const *argv)
{
        char shorts[short_length() + 1];
        struct option longs[lengthof(options) + 1];
        args_fill(shorts, longs);
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
        const char *path = NULL;
        if (optind < argc)
                path = argv[optind++];
        if (optind != argc) {
                fprintf(stderr, "%s: unknown argument: %s\n",
                        PACKAGE_NAME, argv[optind]);
                return EXIT_FAILURE;
        }

        struct nmc_error error;
        if (!nmc_initialize(&error))
                return EXIT_FAILURE;

        if (getenv("NMC_DEBUG"))
                nmc_grammar_debug = 1;

        bool r = path == NULL ? convert_stdin() : convert_path(path);

        nmc_finalize();

        return r ? EXIT_SUCCESS : EXIT_FAILURE;
}
