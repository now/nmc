#ifndef __attribute__
#  if (! defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#    define __attribute__(x) /* empty */
#  endif
#endif

#define NMC_PRINTF(format_index, first_argument_index) \
        __attribute__((format(printf, format_index, first_argument_index)))

struct nmc_error {
        void (*release)(struct nmc_error *);
        int number;
        char *message;
};

bool nmc_error_init(struct nmc_error *error, int number, const char *message);
bool nmc_error_oom(struct nmc_error *error);
bool nmc_error_dyninit(struct nmc_error *error, int number, char *message);
bool nmc_error_formatv(struct nmc_error *error, int number,
                       const char *message, va_list args);
bool nmc_error_format(struct nmc_error *error, int number, const char *message,
                      ...) NMC_PRINTF(3, 4);
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

enum nmc_node_type
{
        NMC_NODE_TYPE_PARENT,
        NMC_NODE_TYPE_AUXILIARY,
        NMC_NODE_TYPE_TEXT,
        NMC_NODE_TYPE_PRIVATE,
};

enum nmc_node_name
{
        NMC_NODE_DOCUMENT,
        NMC_NODE_TITLE,
        NMC_NODE_SECTION,
        NMC_NODE_PARAGRAPH,
        NMC_NODE_ITEMIZATION,
        NMC_NODE_ENUMERATION,
        NMC_NODE_ITEM,
        NMC_NODE_DEFINITIONS,
        NMC_NODE_TERM,
        NMC_NODE_DEFINITION,
        NMC_NODE_QUOTE,
        NMC_NODE_LINE,
        NMC_NODE_ATTRIBUTION,
        NMC_NODE_CODEBLOCK,
        NMC_NODE_TABLE,
        NMC_NODE_HEAD,
        NMC_NODE_BODY,
        NMC_NODE_ROW,
        NMC_NODE_CELL,
        NMC_NODE_FIGURE,
        NMC_NODE_IMAGE,
        NMC_NODE_CODE,
        NMC_NODE_EMPHASIS,
        NMC_NODE_GROUP,
        NMC_NODE_AUXILIARY,
        NMC_NODE_TEXT,
        NMC_NODE_BUFFER,
        NMC_NODE_ANCHOR,
};

struct nmc_node {
        struct nmc_node *next;
        enum nmc_node_type type;
        enum nmc_node_name name;
};

struct nmc_parent_node {
        struct nmc_node node;
        struct nmc_node *children;
};

struct nmc_text_node {
        struct nmc_node node;
        char *text;
};

struct nmc_auxiliary_node_attribute {
        const char *name;
        char *value;
};

struct nmc_auxiliary_node_attributes {
        unsigned int references;
        struct nmc_auxiliary_node_attribute items[];
};

struct nmc_auxiliary_node {
        struct nmc_parent_node node;
        const char *name;
        struct nmc_auxiliary_node_attributes *attributes;
};

#define NMC_NODE_HAS_CHILDREN(node) ((node)->type < NMC_NODE_TYPE_TEXT)
#define nmc_node_children(n) (((struct nmc_parent_node *)(n))->children)

typedef bool (*nmc_node_traverse_fn)(struct nmc_node *node, void *closure);

bool nmc_node_traverse_null(struct nmc_node *node, void *closure);
bool nmc_node_traverse(struct nmc_node *node, nmc_node_traverse_fn enter,
                       nmc_node_traverse_fn leave, void *closure,
                       struct nmc_error *error);
void nmc_node_traverse_r(struct nmc_node *node, nmc_node_traverse_fn enter,
                         nmc_node_traverse_fn leave, void *closure);
void nmc_node_free(struct nmc_node *node);
bool nmc_node_xml(struct nmc_node *node, struct nmc_output *output,
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

struct nmc_node *nmc_parse(const char *input, struct nmc_parser_error **errors);
