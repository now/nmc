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

#define NODE_IS_NESTED(n) ((n)->name <= NODE_AUXILIARY)

void nmc_node_traverse_null(UNUSED(struct node *node), UNUSED(void *closure))
{
}

struct action {
        struct action *next;
        bool enter;
        struct node *nodes;
};

static bool
push(struct action **actions, struct action **used, bool enter, struct node *nodes)
{
        struct action *n;
        if (*used != NULL) {
                n = *used;
                *used = (*used)->next;
        } else {
                n = malloc(sizeof(struct action));
                if (n == NULL)
                        return false;
        }
        n->next = *actions;
        n->enter = enter;
        n->nodes = nodes;
        *actions = n;
        return true;
}

static inline void
pop(struct action **actions, struct action **used)
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
        struct action *actions = NULL;
        struct action *used = NULL;
        if (!push(&actions, &used, true, node))
                return false;
        bool done = false;
        while (actions != NULL) {
                if (actions->enter) {
                        struct node *n = actions->nodes;
                        actions->nodes = n->next;
                        if (actions->nodes == NULL)
                                pop(&actions, &used);
                        enter(n, closure);

                        if (NODE_IS_NESTED(n)) {
                                if (!push(&actions, &used, false, n))
                                        goto oom;
                                if (NMC_NODE_HAS_CHILDREN(n) &&
                                    !push(&actions, &used, true,
                                          ((struct parent_node *)n)->children))
                                        goto oom;
                        }
                } else {
                        leave(actions->nodes, closure);
                        pop(&actions, &used);
                }
        }
        done = true;
oom:
        list_for_each_safe(struct action, p, n, actions)
                free(p);
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
typedef bool (*nmc_output_close_fn)(struct nmc_output *);

struct nmc_output {
        nmc_output_write_fn write;
        nmc_output_close_fn close;
};

static bool
write_all(struct nmc_output *output, const char *string, size_t length, size_t *written)
{
        size_t n = 0;
        while (n < length) {
                ssize_t w = output->write(output, string, length);
                if (w == -1) {
                        *written = n;
                        return false;
                }
                n += w;
        }
        *written = n;
        return true;
}

static inline bool
output_s(struct nmc_output *output, const char *string, size_t length)
{
        size_t w;
        return write_all(output, string, length, &w);
}

static inline bool
output_c(struct nmc_output *output, char c)
{
        return output_s(output, &c, 1);
}

static bool
output_close(struct nmc_output *output)
{
        return output->close == NULL || output->close(output);
}

struct nmc_fd_output {
        struct nmc_output output;
        int fd;
};

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

struct nmc_buffered_output {
        struct nmc_output output;
        struct nmc_output *real;
        size_t length;
        char buffer[4096];
};

static bool
flush(struct nmc_buffered_output *output)
{
        size_t w;
        bool r = write_all(output->real, output->buffer, output->length, &w);
        if (w < output->length)
                memmove(output->buffer, output->buffer + w, output->length - w);
        output->length -= w;
        return r;
}

static ssize_t
nmc_buffered_output_write(struct nmc_buffered_output *output, const char *string, size_t length)
{
        if (output->length == sizeof(output->buffer) && !flush(output))
                return -1;
        size_t r = sizeof(output->buffer) - output->length;
        size_t n = r < length ? r : length;
        memcpy(output->buffer + output->length, string, n);
        output->length += n;
        return n;
}

static bool
nmc_buffered_output_close(struct nmc_buffered_output *output)
{
        bool r = flush(output);
        bool q = output_close(output->real);
        return r && q;
}

struct xml_closure {
        struct nmc_output *output;
        size_t indent;
};

#define MAX_INDENT 64

static void
indent(struct nmc_output *output, int n)
{
        output_c(output, '\n');
        static char cs[2 * MAX_INDENT];
        if (cs[0] == '\0')
                memset(cs, ' ', 2 * MAX_INDENT);
        while (n > 0) {
                size_t i = n > MAX_INDENT ? MAX_INDENT : n;
                output_s(output, cs, 2 * i);
                n -= i;
        }
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
escape(struct nmc_output *output, const char *string, size_t n_entities, const char * const *entities)
{
        const char *s = string;
        const char *e = s;
        while (*e != '\0') {
                if ((unsigned)*e < n_entities && entities[(unsigned)*e] != NULL) {
                        output_s(output, s, e - s);
                        output_s(output, entities[(unsigned)*e], strlen(entities[(unsigned)*e]));
                        s = e + 1;
                }
                e++;
        }
        output_s(output, s, e - s);
}

static void
element_start(struct nmc_output *output, const char *name, size_t length)
{
        output_c(output, '<');
        output_s(output, name, length);
        output_c(output, '>');
}

static void
element_end(struct nmc_output *output, const char *name, size_t length)
{
        output_s(output, "</", 2);
        output_s(output, name, length);
        output_c(output, '>');
}

static void
text_enter(struct node *node, struct xml_closure *closure)
{
        escape(closure->output, ((struct text_node *)node)->text, lengthof(text_entities), text_entities);
}

static void
auxiliary_enter(struct auxiliary_node *node, struct xml_closure *closure)
{
        output_c(closure->output, '<');
        output_s(closure->output, node->name, strlen(node->name));
        for (struct auxiliary_node_attribute *p = node->attributes->items; p->name != NULL; p++) {
                output_c(closure->output, ' ');
                output_s(closure->output, p->name, strlen(p->name));
                output_s(closure->output, "=\"", 2);
                escape(closure->output, p->value, lengthof(attribute_entities), attribute_entities);
                output_c(closure->output, '"');
        }
        output_c(closure->output, '>');
}

static void
auxiliary_leave(struct auxiliary_node *node, struct xml_closure *closure)
{
        element_end(closure->output, node->name, strlen(node->name));
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
        indent(closure->output, closure->indent);
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
        element_start(closure->output, types[node->name].name, types[node->name].length);
        text_enter(node, closure);
}

static void
block_enter(struct node *node, struct xml_closure *closure)
{
        indent(closure->output, closure->indent);
        element_start(closure->output, types[node->name].name, types[node->name].length);
}

static void
leave(struct node *node, struct xml_closure *closure)
{
        element_end(closure->output, types[node->name].name, types[node->name].length);
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
                { (nmc_output_write_fn)nmc_fd_output_write, NULL },
                STDOUT_FILENO
        };
        struct nmc_buffered_output buffer = {
                { (nmc_output_write_fn)nmc_buffered_output_write,
                  (nmc_output_close_fn)nmc_buffered_output_close },
                (struct nmc_output *)&fd,
                0,
                { '\0' }
        };
        struct xml_closure closure = {
                (struct nmc_output *)&buffer,
                0
        };
        static char xml_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        output_s(closure.output, xml_header, sizeof(xml_header) - 1);
        if (!nmc_node_traverse(node, (nmc_node_traverse_fn)xml_enter,
                               (nmc_node_traverse_fn)xml_leave, &closure))
                return false;
        output_c(closure.output, '\n');
        output_close(closure.output);
        return true;
}
