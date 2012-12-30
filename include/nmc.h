struct nmc_error {
        void (*release)(struct nmc_error *);
        int number;
        char *message;
};

void nmc_error_release(struct nmc_error *error);

struct nmc_output;

typedef ssize_t (*nmc_output_write_fn)(struct nmc_output *, const char *,
                                       size_t, struct nmc_error *error);
typedef bool (*nmc_output_close_fn)(struct nmc_output *, struct nmc_error *error);

struct nmc_output {
        nmc_output_write_fn write;
        nmc_output_close_fn close;
};

bool nmc_output_write_all(struct nmc_output *output, const char *string,
                          size_t length, size_t *written,
                          struct nmc_error *error);
bool nmc_output_close(struct nmc_output *output, struct nmc_error *error);

struct nmc_fd_output {
        struct nmc_output output;
        int fd;
};

void nmc_fd_output_init(struct nmc_fd_output *output, int fd);

struct nmc_buffered_output {
        struct nmc_output output;
        struct nmc_output *real;
        size_t length;
        char buffer[4096];
};

void nmc_buffered_output_init(struct nmc_buffered_output *output,
                              struct nmc_output *real);

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
#define nmc_node_children(n) (((struct parent_node *)(n))->children)

typedef bool (*nmc_node_traverse_fn)(struct node *node, void *closure);

bool nmc_node_traverse_null(struct node *node, void *closure);
bool nmc_node_traverse(struct node *node, nmc_node_traverse_fn enter,
                       nmc_node_traverse_fn leave, void *closure,
                       struct nmc_error *error);
void nmc_node_traverse_r(struct node *node, nmc_node_traverse_fn enter,
                         nmc_node_traverse_fn leave, void *closure);
void nmc_node_free(struct node *node);
bool nmc_node_xml(struct node *node, struct nmc_output *output,
                  struct nmc_error *error);

struct nmc_location {
        int first_line;
        int last_line;
        int first_column;
        int last_column;
};

char *nmc_location_str(const struct nmc_location *location);

struct nmc_parser_error {
        struct nmc_parser_error *next;
        struct nmc_location location;
        char *message;
};

extern struct nmc_parser_error nmc_parser_oom_error;

void nmc_parser_error_free(struct nmc_parser_error *error);

extern int nmc_grammar_debug;

bool nmc_initialize(struct nmc_error *error);
void nmc_finalize(void);

struct node *nmc_parse(const char *input, struct nmc_parser_error **errors);
