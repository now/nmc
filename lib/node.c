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

#include "error.h"

#define NODE_IS_NESTED(n) ((n)->name <= NODE_AUXILIARY)

bool
nmc_node_traverse_null(UNUSED(struct node *node), UNUSED(void *closure))
{
        return true;
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
                  nmc_node_traverse_fn leave, void *closure,
                  struct nmc_error *error)
{
        if (node == NULL)
                return true;
        bool done = false;
        struct action *actions = NULL;
        struct action *used = NULL;
        if (!push(&actions, &used, true, node))
                goto oom;
        while (actions != NULL) {
                if (actions->enter) {
                        struct node *n = actions->nodes;
                        actions->nodes = n->next;
                        if (actions->nodes == NULL)
                                pop(&actions, &used);
                        // TODO Pass error to enter/leave?
                        if (!enter(n, closure))
                                goto fail;

                        if (NODE_IS_NESTED(n)) {
                                if (!push(&actions, &used, false, n))
                                        goto oom;
                                if (NMC_NODE_HAS_CHILDREN(n) &&
                                    !push(&actions, &used, true,
                                          ((struct parent_node *)n)->children))
                                        goto oom;
                        }
                } else {
                        if (!leave(actions->nodes, closure))
                                goto fail;
                        pop(&actions, &used);
                }
        }
        done = true;
oom:
        nmc_error_oom(error);
fail:
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

typedef ssize_t (*nmc_output_write_fn)(struct nmc_output *, const char *,
                                       size_t, struct nmc_error *error);
typedef bool (*nmc_output_close_fn)(struct nmc_output *, struct nmc_error *error);

struct nmc_output {
        nmc_output_write_fn write;
        nmc_output_close_fn close;
};

static bool
write_all(struct nmc_output *output, const char *string, size_t length,
          size_t *written, struct nmc_error *error)
{
        size_t n = 0;
        while (n < length) {
                ssize_t w = output->write(output, string, length, error);
                if (w == -1) {
                        *written = n;
                        return false;
                }
                n += w;
        }
        *written = n;
        return true;
}

struct nmc_fd_output {
        struct nmc_output output;
        int fd;
};

static bool
output_close(struct nmc_output *output, struct nmc_error *error)
{
        return output->close == NULL || output->close(output, error);
}

static ssize_t
nmc_fd_output_write(struct nmc_fd_output *output, const char *string,
                    size_t length, struct nmc_error *error)
{
        if (length == 0)
                return 0;
        while (true) {
                ssize_t w = write(output->fd, string, length);
                if (w == -1) {
                        if (errno == EAGAIN || errno == EINTR)
                                continue;
                        nmc_error_init(error, errno, "can’t write to file");
                }
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
flush(struct nmc_buffered_output *output, struct nmc_error *error)
{
        size_t w;
        bool r = write_all(output->real, output->buffer, output->length, &w, error);
        if (w < output->length)
                memmove(output->buffer, output->buffer + w, output->length - w);
        output->length -= w;
        return r;
}

static ssize_t
nmc_buffered_output_write(struct nmc_buffered_output *output,
                          const char *string, size_t length,
                          struct nmc_error *error)
{
        if (output->length == sizeof(output->buffer) && !flush(output, error))
                return -1;
        size_t r = sizeof(output->buffer) - output->length;
        size_t n = r < length ? r : length;
        memcpy(output->buffer + output->length, string, n);
        output->length += n;
        return n;
}

static bool
nmc_buffered_output_close(struct nmc_buffered_output *output,
                          struct nmc_error *error)
{
        if (flush(output, error))
                return output_close(output->real, error);
        struct nmc_error *ignored;
        output_close(output->real, ignored);
        return false;
}

struct xml_closure {
        struct nmc_output *output;
        size_t indent;
        struct nmc_error *error;
};

static inline bool
outs(struct xml_closure *closure, const char *string, size_t length)
{
        size_t w;
        return write_all(closure->output, string, length, &w, closure->error);
}

static inline bool
outc(struct xml_closure *closure, char c)
{
        return outs(closure, &c, 1);
}

#define MAX_INDENT 64

static bool
indent(struct xml_closure *closure, int n)
{
        if (!outc(closure, '\n'))
                return false;
        static char cs[2 * MAX_INDENT];
        if (cs[0] == '\0')
                memset(cs, ' ', 2 * MAX_INDENT);
        while (n > 0) {
                size_t i = n > MAX_INDENT ? MAX_INDENT : n;
                if (!outs(closure, cs, 2 * i))
                        return false;
                n -= i;
        }
        return true;
}

struct entity {
        const char *s;
        size_t n;
};

#define E(s) { s, sizeof(s) - 1 }
#define N { NULL, 0 }
static const struct entity text_entities[] = {
        N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,
        N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,
        N,N,N,N,N,N,E("&amp;"),N,N,N,N,N,N,N,N,N,
        N,N,N,N,N,N,N,N,N,N,N,N,E("&lt;"),N,E("&gt;")
};

static const struct entity attribute_entities[] = {
        N,N,N,N,N,N,N,N,N,E("&#9;"),E("&#10;"),N,N,E("&#13;"),N,N,
        N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,N,
        N,N,E("&quot;"),N,N,N,E("&amp;"),N,N,N,N,N,N,N,N,N,
        N,N,N,N,N,N,N,N,N,N,N,N,E("&lt;"),N,E("&gt;")
};
#undef N
#undef E

static bool
escape(struct xml_closure *closure, const char *string, size_t n_entities,
       const struct entity *entities)
{
        const char *s = string;
        const char *e = s;
        while (*e != '\0') {
                const struct entity *p;
                if ((unsigned char)*e < n_entities &&
                    (p = &entities[(unsigned char)*e])->n > 0) {
                        if (!(outs(closure, s, e - s) &&
                              outs(closure, p->s, p->n)))
                                return false;
                        s = e + 1;
                }
                e++;
        }
        return outs(closure, s, e - s);
}

static bool
element_start(struct xml_closure *closure, const char *name, size_t n)
{
        return outc(closure, '<') && outs(closure, name, n) && outc(closure, '>');
}

static bool
element_end(struct xml_closure *closure, const char *name, size_t n)
{
        return outs(closure, "</", 2) && outs(closure, name, n) && outc(closure, '>');
}

static bool
text_enter(struct node *node, struct xml_closure *closure)
{
        return escape(closure, ((struct text_node *)node)->text,
                      lengthof(text_entities), text_entities);
}

static inline bool
outattributes(struct xml_closure *closure,
              struct auxiliary_node_attribute *attributes)
{
        for (struct auxiliary_node_attribute *p = attributes; p->name != NULL; p++) {
                if (!(outc(closure, ' ') &&
                      outs(closure, p->name, strlen(p->name)) &&
                      outs(closure, "=\"", 2) &&
                      escape(closure, p->value,
                             lengthof(attribute_entities), attribute_entities) &&
                      outc(closure, '"')))
                        return false;
        }
        return true;
}

static bool
auxiliary_enter(struct auxiliary_node *node, struct xml_closure *closure)
{
        return outc(closure, '<') &&
                outs(closure, node->name, strlen(node->name)) &&
                outattributes(closure, node->attributes->items) &&
                outc(closure, '>');
}

static bool
auxiliary_leave(struct auxiliary_node *node, struct xml_closure *closure)
{
        return element_end(closure, node->name, strlen(node->name));
}

static bool block_enter(struct node *node, struct xml_closure *closure);

static bool
text_block_enter(struct node *node, struct xml_closure *closure)
{
        return block_enter(node, closure) && text_enter(node, closure);
}

static bool
indenting_block_enter(struct node *node, struct xml_closure *closure)
{
        if (!block_enter(node, closure))
                return false;
        closure->indent++;
        return true;
}

static bool leave(struct node *node, struct xml_closure *closure);

static bool
indenting_block_leave(struct node *node, struct xml_closure *closure)
{
        closure->indent--;
        return indent(closure, closure->indent) &&
                leave(node, closure);
}

static bool inline_enter(struct node *node, struct xml_closure *closure);

typedef bool (*xmltraversefn)(struct node *, struct xml_closure *);

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

static bool
inline_enter(struct node *node, struct xml_closure *closure)
{
        return element_start(closure, types[node->name].name,
                             types[node->name].length) &&
                text_enter(node, closure);
}

static bool
block_enter(struct node *node, struct xml_closure *closure)
{
        return indent(closure, closure->indent) &&
                element_start(closure, types[node->name].name,
                              types[node->name].length);
}

static bool
leave(struct node *node, struct xml_closure *closure)
{
        return element_end(closure, types[node->name].name,
                           types[node->name].length);
}

static bool
xml_enter(struct node *node, struct xml_closure *closure)
{
        return types[node->name].enter(node, closure);
}

static bool
xml_leave(struct node *node, struct xml_closure *closure)
{
        return types[node->name].leave(node, closure);
}

bool
nmc_node_xml(struct node *node, struct nmc_error *error)
{
        struct xml_closure closure = {
                (struct nmc_output *)&(struct nmc_buffered_output){
                        { (nmc_output_write_fn)nmc_buffered_output_write,
                          (nmc_output_close_fn)nmc_buffered_output_close },
                        (struct nmc_output *)&(struct nmc_fd_output){
                                { (nmc_output_write_fn)nmc_fd_output_write, NULL },
                                STDOUT_FILENO
                        },
                        0,
                        { '\0' }
                },
                0,
                error
        };
        static char xml_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        if (!outs(&closure, xml_header, sizeof(xml_header) - 1))
                return false;
        if (!nmc_node_traverse(node, (nmc_node_traverse_fn)xml_enter,
                               (nmc_node_traverse_fn)xml_leave, &closure, error))
                return false;
        return outc(&closure, '\n') &&
                output_close(closure.output, error);
}
