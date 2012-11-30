enum node_type
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
        NODE_BUFFER,
        NODE_TEXT,
        NODE_ANCHOR,
        NODE_AUXILIARY
};

struct node
{
        struct node *next;
        enum node_type type;
};

struct parent_node
{
        struct node node;
        struct node *children;
};

struct text_node
{
        struct node node;
        char *text;
};

struct auxiliary_node_attribute
{
        const char *name;
        char *value;
};

struct auxiliary_node
{
        struct parent_node node;
        const char *name;
        struct auxiliary_node_attribute *attributes;
};

typedef void (*traversefn)(struct node *node, void *closure);

void nmc_node_traverse_null(struct node *node, void *closure);
void nmc_node_traverse(struct node *node, traversefn enter, traversefn leave, void *closure);
void nmc_node_traverse_r(struct node *node, traversefn enter, traversefn leave, void *closure);
void node_free(struct node *node);
void nmc_node_to_xml(struct node *node);

struct nmc_location
{
        int first_line;
        int last_line;
        int first_column;
        int last_column;
};

char *nmc_location_str(const struct nmc_location *location);

void nmc_grammar_initialize(void);
void nmc_grammar_finalize(void);

struct nmc_parser_error
{
        struct nmc_parser_error *next;
        struct nmc_location location;
        char *message;
};

struct nmc_parser_error *nmc_parser_error_newv(struct nmc_location *location,
                                               const char *message,
                                               va_list args);
struct nmc_parser_error *nmc_parser_error_new(struct nmc_location *location,
                                              const char *message,
                                              ...) PRINTF(2, 3);
void nmc_parser_error_free(struct nmc_parser_error *error);

struct node *nmc_parse(const char *input, struct nmc_parser_error **errors);
