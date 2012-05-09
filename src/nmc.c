#include <config.h>

#include <libxml/tree.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        xmlDocPtr doc = xmlNewDoc("1.0");
        xmlNodePtr root = xmlNewNode(NULL, "nml");
        xmlDocSetRootElement(doc, root);

        char buffer[4096];
        ssize_t bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
        xmlNodePtr title = xmlNewChild(root, NULL, "title", NULL);
        xmlNodeAddContentLen(title, buffer, bytes - 1);

        xmlSaveFormatFileEnc("-", doc, "UTF-8", 1);

        xmlFreeDoc(doc);
        xmlCleanupParser();

        return EXIT_SUCCESS;
}

