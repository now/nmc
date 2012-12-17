enum node_type
{
        PARENT,
        AUXILIARY,
        TEXT,
        PRIVATE,
};

enum node_name
{
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
        NODE_AUXILIARY,
        NODE_TEXT,
        NODE_BUFFER,
        NODE_ANCHOR,
};

struct node {
        struct node *next;
        enum node_type type;
        enum node_name name;
};

struct parent_node {
        struct node node;
        struct node *children;
};

struct text_node {
        struct node node;
        char *text;
};

struct auxiliary_node_attribute {
        const char *name;
        char *value;
};

struct auxiliary_node_attributes {
        unsigned int references;
        struct auxiliary_node_attribute items[];
};

struct auxiliary_node {
        struct parent_node node;
        const char *name;
        struct auxiliary_node_attributes *attributes;
};

#define NMC_NODE_HAS_CHILDREN(node) ((node)->type < TEXT)

typedef void (*nmc_node_traverse_fn)(struct node *node, void *closure);

void nmc_node_traverse_null(struct node *node, void *closure);
void nmc_node_traverse(struct node *node, nmc_node_traverse_fn enter,
                       nmc_node_traverse_fn leave, void *closure);
void nmc_node_traverse_r(struct node *node, nmc_node_traverse_fn enter,
                         nmc_node_traverse_fn leave, void *closure);
void nmc_node_free(struct node *node);
void nmc_node_xml(struct node *node);

struct nmc_location {
        int first_line;
        int last_line;
        int first_column;
        int last_column;
};

char *nmc_location_str(const struct nmc_location *location);

struct nmc_error {
        struct nmc_error *next;
        struct nmc_location location;
        char *message;
};

void nmc_error_free(struct nmc_error *error);

extern int nmc_grammar_debug;

bool nmc_initialize(struct nmc_error **error);
void nmc_finalize(void);

struct node *nmc_parse(const char *input, struct nmc_error **errors);
