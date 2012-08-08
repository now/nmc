#include <config.h>

#include <libxml/tree.h>
#include <stdbool.h>

#include "validator.h"

struct nmc_validator
{
        xmlListPtr footnotes;
        xmlListPtr errors;
};

struct nmc_reference_closure
{
        const xmlChar *id;
        bool found;
};

static void
error(struct nmc_validator *validator, const char *message, ...)
{
        va_list args;
        xmlChar buf[1];

        va_start(args, message);
        int size = xmlStrVPrintf(buf, sizeof(buf), (const xmlChar *)message, args);
        va_end(args);

        char *error = (char *)xmlMalloc(size);
        va_start(args, message);
        xmlStrVPrintf((xmlChar *)error, size, (const xmlChar *)message, args);
        va_end(args);

        xmlListPushBack(validator->errors, error);
}

static void
error_free(xmlLinkPtr link)
{
        xmlFree(xmlLinkGetData(link));
}

static const xmlChar *
id(xmlNodePtr node)
{
        return xmlHasProp(node, BAD_CAST "id")->children->content;
}

static int
validate_reference(xmlNodePtr footnotes, struct nmc_reference_closure *closure)
{
        for (xmlNodePtr p = footnotes->children; p != NULL; p = p->next)
                if (xmlStrEqual(id(p), closure->id)) {
                        xmlNewProp(p, BAD_CAST "referenced", BAD_CAST "true");
                        closure->found = true;
                        return 0;
                }
        return 1;
}

static void
validate_footnotes(struct nmc_validator *validator, xmlNodePtr footnotes)
{
        for (xmlNodePtr p = footnotes->children; p != NULL; p = p->next)
                if (!xmlHasProp(p, BAD_CAST "referenced"))
                        error(validator, "unreferenced footnote: %s\n", id(p));
}

static void
validate_node(struct nmc_validator *validator, xmlNodePtr node)
{
        if (node->type != XML_ELEMENT_NODE)
                return;

        if (xmlStrEqual(node->name, BAD_CAST "footnoted")) {
                xmlListPushFront(validator->footnotes, xmlGetLastChild(node));
                validate_node(validator, node->children);
                xmlNodePtr footnotes = xmlLinkGetData(xmlListFront(validator->footnotes));
                xmlListPopFront(validator->footnotes);
                validate_footnotes(validator, footnotes);
                return;
        } else if (xmlStrEqual(node->name, BAD_CAST "reference")) {
                struct nmc_reference_closure closure = { id(node), false };
                xmlListWalk(validator->footnotes,
                            (xmlListWalker)validate_reference,
                            &closure);
                if (!closure.found)
                        error(validator,
                              "reference to undefined footnote: %s",
                              closure.id);
        }

        for (xmlNodePtr p = node->children; p != NULL; p = p->next)
                validate_node(validator, p);
}

xmlListPtr
nmc_validate(xmlDocPtr doc)
{
        struct nmc_validator validator;
        validator.footnotes = xmlListCreate(NULL, NULL);
        validator.errors = xmlListCreate(error_free, NULL);

        validate_node(&validator, xmlDocGetRootElement(doc));

        return validator.errors;
}
