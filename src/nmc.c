#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"
#include "validator.h"

extern int nmc_grammar_debug;

static int
report_error(const char *error)
{
        fputs(error, stderr);
        return 1;
}

int
main(UNUSED(int argc), UNUSED(char **argv))
{
        char buffer[4096];
        ssize_t bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
        buffer[bytes] = '\0';

        if (getenv("NMC_DEBUG"))
                nmc_grammar_debug = 1;
        xmlDocPtr doc = nmc_parse(BAD_CAST buffer);
        xmlListPtr errors = nmc_validate(doc);
        xmlListWalk(errors, (xmlListWalker)report_error, NULL);
        xmlListDelete(errors);
        xmlSaveFormatFileEnc("-", doc, "UTF-8", 1);
        xmlFreeDoc(doc);
        xmlCleanupParser();

        return EXIT_SUCCESS;
}

