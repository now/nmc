#ifndef SRC_NODE_H
#define SRC_NODE_H

enum node_type {
        NODE_DOCUMENT,
        NODE_TITLE,
        NODE_PARAGRAPH,
        NODE_ITEMIZATION,
        NODE_ENUMERATION,
        NODE_DEFINITIONS,
        NODE_DEFINITION,
        NODE_TERM,
        NODE_ITEM,
        NODE_QUOTE,
        NODE_LINE,
        NODE_ATTRIBUTION,
        NODE_CODEBLOCK,
        NODE_TABLE,
        NODE_HEAD,
        NODE_BODY,
        NODE_ROW,
        NODE_ENTRY,
        NODE_SECTION,
        NODE_CODE,
        NODE_EMPHASIS,
        NODE_GROUP,
        NODE_BUFFER,
        NODE_TEXT,
        NODE_ANCHOR,
        NODE_AUXILIARY
};

struct node
{
        struct node *next;
        enum node_type type;
        union {
                struct node *children;
                char *text;
                struct nmc_string *buffer;
                struct anchor *anchor;
        } u;
};

struct auxiliary_node_attribute
{
        const char *name;
        char *value;
};

struct auxiliary_node
{
        struct node node;
        const char *name;
        struct auxiliary_node_attribute *attributes;
};

typedef void (*traversefn)(struct node *node, void *closure);

void nmc_node_traverse_null(struct node *node, void *closure);
void nmc_node_traverse(struct node *node, traversefn enter, traversefn leave, void *closure);
void nmc_node_traverse_r(struct node *node, traversefn enter, traversefn leave, void *closure);
void node_free(struct node *node);
void nmc_node_to_xml(struct node *node);

#endif
