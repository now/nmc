#include <config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nmc.h>
#include <nmc/list.h>

#include <private.h>
#include <buffer.h>

#define NODE_IS_NESTED(n) ((n)->name <= NODE_AUXILIARY)

void nmc_node_traverse_null(UNUSED(struct node *node), UNUSED(void *closure))
{
}

struct action {
        struct action *next;
        bool enter;
        struct node *nodes;
};

static struct action *
action_new(struct action **used, struct action *next, bool enter, struct node *nodes)
{
        struct action *n;
        if (*used != NULL) {
                n = *used;
                *used = (*used)->next;
        } else {
                n = malloc(sizeof(struct action));
                if (n == NULL) {
                        list_for_each_safe(struct action, p, m, next)
                                free(p);
                        return NULL;
                }
        }
        n->next = next;
        n->enter = enter;
        n->nodes = nodes;
        return n;
}

static inline void
move_to_used(struct action **actions, struct action **used)
{
        struct action *a = *actions;
        *actions = (*actions)->next;
        a->next = *used;
        *used = a;
}

bool
nmc_node_traverse(struct node *node, nmc_node_traverse_fn enter,
                  nmc_node_traverse_fn leave, void *closure)
{
        if (node == NULL)
                return true;
        struct action *used = NULL;
        struct action *actions = action_new(&used, NULL, true, node);
        if (actions == NULL)
                return false;
        bool done = false;
        while (actions != NULL) {
                if (actions->enter) {
                        struct node *n = actions->nodes;
                        actions->nodes = n->next;
                        if (actions->nodes == NULL)
                                move_to_used(&actions, &used);
                        enter(n, closure);

                        if (NODE_IS_NESTED(n)) {
                                actions = action_new(&used, actions, false, n);
                                if (actions == NULL)
                                        goto oom;
                                if (NMC_NODE_HAS_CHILDREN(n)) {
                                        actions = action_new(&used, actions, true, ((struct parent_node *)n)->children);
                                        if (actions == NULL)
                                                goto oom;
                                }
                        }
                } else {
                        leave(actions->nodes, closure);
                        move_to_used(&actions, &used);
                }
        }
        done = true;
oom:
        list_for_each_safe(struct action, p, n, used)
                free(p);
        return done;
}

void
nmc_node_traverse_r(struct node *node, nmc_node_traverse_fn enter,
                    nmc_node_traverse_fn leave, void *closure)
{
        list_for_each(struct node, p, node) {
                enter(p, closure);
                if (NODE_IS_NESTED(p)) {
                        if (NMC_NODE_HAS_CHILDREN(p))
                                nmc_node_traverse_r(((struct parent_node *)p)->children, enter, leave, closure);
                        leave(p, closure);
                }
        }
}

struct nmc_output;

typedef ssize_t (*nmc_output_write_fn)(struct nmc_output *, const char *, size_t);

struct nmc_output {
        nmc_output_write_fn write;
};

struct nmc_fd_output {
        struct nmc_output output;
        int fd;
};

struct output_buffer {
        struct nmc_output *output;
        struct buffer buffer;
};

static inline bool
output_buffer_flush(struct output_buffer *buffer)
{
        if (buffer->output->write(buffer->output, buffer->buffer.content,
                                  buffer->buffer.length) == -1)
                return false;
        buffer->buffer.length = 0;
        return true;
}

#define OUTPUT_BUFFER_LIMIT 4096

static inline bool
output_buffer_flush_if_full(struct output_buffer *buffer)
{
        return buffer->buffer.length < OUTPUT_BUFFER_LIMIT ? true :
                output_buffer_flush(buffer);
}

static inline bool
output_buffer_c(struct output_buffer *buffer, char c, size_t n)
{
        return buffer_append_c(&buffer->buffer, c, n) &&
                output_buffer_flush_if_full(buffer);
}

static inline bool
output_buffer_s(struct output_buffer *buffer, const char *string, size_t length)
{
        if (length >= OUTPUT_BUFFER_LIMIT) {
                return output_buffer_flush(buffer) &&
                        buffer->output->write(buffer->output, string, length) != -1;
        }
        return buffer_append(&buffer->buffer, string, length) &&
                output_buffer_flush_if_full(buffer);
}

static ssize_t
nmc_fd_output_write(struct nmc_fd_output *output, const char *string, size_t length)
{
        if (length == 0)
                return 0;
        while (true) {
                ssize_t w = write(output->fd, string, length);
                if (w == -1 && (errno == EAGAIN || errno == EINTR))
                        continue;
                return w;
        }
}

struct xml_closure {
        struct output_buffer buffer;
        int indent;
};

static void
indent(struct output_buffer *buffer, int n)
{
        output_buffer_c(buffer, '\n', 1);
        output_buffer_c(buffer, ' ', 2 * n);
}

static const char * const text_entities[] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, "&amp;", NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, "&lt;", NULL, "&gt;"
};

static const char * const attribute_entities[] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, "&#9;", "&#10;", NULL, NULL, "&#13;", NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, "&#quot;", NULL, NULL, NULL, "&amp;", NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, "&lt;", NULL, "&gt;"
};

static void
escape(struct output_buffer *buffer, const char *string, size_t n_entities, const char * const *entities)
{
        const char *s = string;
        const char *e = s;
        while (*e != '\0') {
                if ((unsigned)*e < n_entities && entities[(unsigned)*e] != NULL) {
                        output_buffer_s(buffer, s, e - s);
                        output_buffer_s(buffer, entities[(unsigned)*e], strlen(entities[(unsigned)*e]));
                        s = e + 1;
                }
                e++;
        }
        output_buffer_s(buffer, s, e - s);
}

static void
element_start(struct output_buffer *buffer, const char *name, size_t length)
{
        output_buffer_c(buffer, '<', 1);
        output_buffer_s(buffer, name, length);
        output_buffer_c(buffer, '>', 1);
}

static void
element_end(struct output_buffer *buffer, const char *name, size_t length)
{
        output_buffer_s(buffer, "</", 2);
        output_buffer_s(buffer, name, length);
        output_buffer_c(buffer, '>', 1);
}

static void
text_enter(struct node *node, struct xml_closure *closure)
{
        escape(&closure->buffer, ((struct text_node *)node)->text, lengthof(text_entities), text_entities);
}

static void
auxiliary_enter(struct auxiliary_node *node, struct xml_closure *closure)
{
        output_buffer_c(&closure->buffer, '<', 1);
        output_buffer_s(&closure->buffer, node->name, strlen(node->name));
        for (struct auxiliary_node_attribute *p = node->attributes->items; p->name != NULL; p++) {
                output_buffer_c(&closure->buffer, ' ', 1);
                output_buffer_s(&closure->buffer, p->name, strlen(p->name));
                output_buffer_s(&closure->buffer, "=\"", 2);
                escape(&closure->buffer, p->value, lengthof(attribute_entities), attribute_entities);
                output_buffer_c(&closure->buffer, '"', 1);
        }
        output_buffer_c(&closure->buffer, '>', 1);
}

static void
auxiliary_leave(struct auxiliary_node *node, struct xml_closure *closure)
{
        element_end(&closure->buffer, node->name, strlen(node->name));
}

static void block_enter(struct node *node, struct xml_closure *closure);

static void
text_block_enter(struct node *node, struct xml_closure *closure)
{
        block_enter(node, closure);
        text_enter(node, closure);
}

static void
indenting_block_enter(struct node *node, struct xml_closure *closure)
{
        block_enter(node, closure);
        closure->indent++;
}

static void leave(struct node *node, struct xml_closure *closure);

static void
indenting_block_leave(struct node *node, struct xml_closure *closure)
{
        closure->indent--;
        indent(&closure->buffer, closure->indent);
        leave(node, closure);
}

static void inline_enter(struct node *node, struct xml_closure *closure);

typedef void (*xmltraversefn)(struct node *, struct xml_closure *);

static struct {
        const char *name;
        size_t length;
        xmltraversefn enter;
        xmltraversefn leave;
} types[] = {
#define indenting_block indenting_block_enter, indenting_block_leave
#define text_block text_block_enter, leave
#define block block_enter, leave
#define inline inline_enter, leave
#define NAME(n) n, sizeof(n) - 1
        [NODE_DOCUMENT] = { NAME("nml"), indenting_block },
        [NODE_TITLE] = { NAME("title"), block },
        [NODE_PARAGRAPH] = { NAME("p"), block },
        [NODE_ITEMIZATION] = { NAME("itemization"), indenting_block },
        [NODE_ENUMERATION] = { NAME("enumeration"), indenting_block },
        [NODE_DEFINITIONS] = { NAME("definitions"), indenting_block },
        [NODE_DEFINITION] = { NAME("definition"), indenting_block },
        [NODE_TERM] = { NAME("term"), text_block },
        [NODE_ITEM] = { NAME("item"), indenting_block },
        [NODE_QUOTE] = { NAME("quote"), indenting_block },
        [NODE_LINE] = { NAME("line"), block },
        [NODE_ATTRIBUTION] = { NAME("attribution"), block },
        [NODE_CODEBLOCK] = { NAME("code"), text_block },
        [NODE_TABLE] = { NAME("table"), indenting_block },
        [NODE_HEAD] = { NAME("head"), indenting_block },
        [NODE_BODY] = { NAME("body"), indenting_block },
        [NODE_ROW] = { NAME("row"), indenting_block },
        [NODE_ENTRY] = { NAME("entry"), block },
        [NODE_SECTION] = { NAME("section"), indenting_block },
        [NODE_CODE] = { NAME("code"), inline },
        [NODE_EMPHASIS] = { NAME("emphasis"), inline },
        [NODE_GROUP] = { NULL, 0, (xmltraversefn)nmc_node_traverse_null, (xmltraversefn)nmc_node_traverse_null },
        [NODE_AUXILIARY] = { NULL, 0, (xmltraversefn)auxiliary_enter, (xmltraversefn)auxiliary_leave },
        /* TODO Could have assertions for the NULLs here instead. */
        [NODE_TEXT] = { NULL, 0, text_enter, NULL },
        [NODE_BUFFER] = { NULL, 0, NULL, NULL },
        [NODE_ANCHOR] = { NULL, 0, NULL, NULL },
#undef NAME
#undef indenting_block
#undef text_block
#undef block
#undef inline
};

static void
inline_enter(struct node *node, struct xml_closure *closure)
{
        element_start(&closure->buffer, types[node->name].name, types[node->name].length);
        text_enter(node, closure);
}

static void
block_enter(struct node *node, struct xml_closure *closure)
{
        indent(&closure->buffer, closure->indent);
        element_start(&closure->buffer, types[node->name].name, types[node->name].length);
}

static void
leave(struct node *node, struct xml_closure *closure)
{
        element_end(&closure->buffer, types[node->name].name, types[node->name].length);
}

static void
xml_enter(struct node *node, struct xml_closure *closure)
{
        types[node->name].enter(node, closure);
}

static void
xml_leave(struct node *node, struct xml_closure *closure)
{
        types[node->name].leave(node, closure);
}

bool
nmc_node_xml(struct node *node)
{
        struct nmc_fd_output fd = {
                { (nmc_output_write_fn)nmc_fd_output_write },
                STDOUT_FILENO
        };
        struct xml_closure closure = {
                { (struct nmc_output *)&fd, BUFFER_INIT },
                0
        };
        static char xml_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        output_buffer_s(&closure.buffer, xml_header, sizeof(xml_header) - 1);
        if (!nmc_node_traverse(node, (nmc_node_traverse_fn)xml_enter,
                               (nmc_node_traverse_fn)xml_leave, &closure))
                return false;
        output_buffer_c(&closure.buffer, '\n', 1);
        output_buffer_flush(&closure.buffer);
        free(closure.buffer.buffer.content);
        return true;
}
