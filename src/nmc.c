#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"

extern int nmc_grammar_debug;

static int
report_nmc_parser_error(const struct nmc_parser_error *error)
{
        if (error->location.first_line == error->location.last_line) {
                if (error->location.first_column == error->location.last_column)
                        fprintf(stderr, "%d:%d",
                                error->location.first_line,
                                error->location.first_column);
                else
                        fprintf(stderr, "%d.%d-%d",
                                error->location.first_line,
                                error->location.first_column,
                                error->location.last_column);
        } else
                fprintf(stderr, "%d.%d-%d.%d",
                        error->location.first_line,
                        error->location.first_column,
                        error->location.last_line,
                        error->location.last_column);
        fprintf(stderr, ": %s\n", error->message);

        return 1;
}

int
main(UNUSED(int argc), UNUSED(char **argv))
{
        int result = EXIT_SUCCESS;

        char *buffer = xmlMalloc(2000000);
        ssize_t bytes = read(STDIN_FILENO, buffer, 2000000);
        buffer[bytes] = '\0';

        if (getenv("NMC_DEBUG"))
                nmc_grammar_debug = 1;

        xmlInitParser();

        xmlListPtr errors;
        xmlDocPtr doc = nmc_parse(BAD_CAST buffer, &errors);
        if (!xmlListEmpty(errors))
                result = EXIT_FAILURE;
        xmlListWalk(errors, (xmlListWalker)report_nmc_parser_error, NULL);
        xmlListDelete(errors);
        xmlSaveFormatFileEnc("-", doc, "UTF-8", 1);
        xmlFreeDoc(doc);

        xmlCleanupParser();

        return result;
}

