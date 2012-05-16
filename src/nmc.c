#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>
#include <unistd.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"

int
main(UNUSED(int argc), UNUSED(char **argv))
{
        char buffer[4096];
        ssize_t bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
        buffer[bytes] = '\0';

        xmlDocPtr doc = nmc_parse(BAD_CAST buffer);
        xmlSaveFormatFileEnc("-", doc, "UTF-8", 1);
        xmlFreeDoc(doc);
        xmlCleanupParser();

        return EXIT_SUCCESS;
}

