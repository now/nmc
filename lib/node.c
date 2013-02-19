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

#define NODE_IS_NESTED(n) ((n)->name < NMC_NODE_TEXT)

bool
nmc_node_traverse_null(UNUSED(struct nmc_node *node), UNUSED(void *closure))
{
        return true;
}

struct action {
        struct action *next;
        bool enter;
        struct nmc_node *nodes;
};

static bool
push(struct action **actions, struct action **used, bool enter, struct nmc_node *nodes)
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
nmc_node_traverse(struct nmc_node *node, nmc_node_traverse_fn enter,
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
                        struct nmc_node *n = actions->nodes;
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
                                    nmc_node_children(n) != NULL &&
                                    !push(&actions, &used, true,
                                          nmc_node_children(n)))
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
nmc_node_traverse_r(struct nmc_node *node, nmc_node_traverse_fn enter,
                    nmc_node_traverse_fn leave, void *closure)
{
        list_for_each(struct nmc_node, p, node) {
                enter(p, closure);
                if (NODE_IS_NESTED(p)) {
                        if (NMC_NODE_HAS_CHILDREN(p))
                                nmc_node_traverse_r(nmc_node_children(p), enter, leave, closure);
                        leave(p, closure);
                }
        }
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
        return nmc_output_write_all(closure->output, string, length, &w, closure->error);
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
text_enter(struct nmc_node *node, struct xml_closure *closure)
{
        return escape(closure, ((struct nmc_text_node *)node)->text,
                      lengthof(text_entities), text_entities);
}

static bool block_enter(struct nmc_node *node, struct xml_closure *closure);

static bool
text_block_enter(struct nmc_node *node, struct xml_closure *closure)
{
        return block_enter(node, closure) && text_enter(node, closure);
}

static bool
indenting_block_enter(struct nmc_node *node, struct xml_closure *closure)
{
        if (!block_enter(node, closure))
                return false;
        closure->indent++;
        return true;
}

static bool data_enter(struct nmc_data_node *node, struct xml_closure *closure);

static bool
data_block_enter(struct nmc_data_node *node, struct xml_closure *closure)
{
        return indent(closure, closure->indent) &&
                data_enter(node, closure);
}

static bool leave(struct nmc_node *node, struct xml_closure *closure);

static bool
indenting_block_leave(struct nmc_node *node, struct xml_closure *closure)
{
        closure->indent--;
        return indent(closure, closure->indent) &&
                leave(node, closure);
}

static bool inline_enter(struct nmc_node *node, struct xml_closure *closure);

typedef bool (*xmltraversefn)(struct nmc_node *, struct xml_closure *);

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
#define data (xmltraversefn)data_enter, leave
#define data_block (xmltraversefn)data_block_enter, leave
#define NAME(n) n, sizeof(n) - 1
        [NMC_NODE_DOCUMENT] = { NAME("nml"), indenting_block },
        [NMC_NODE_TITLE] = { NAME("title"), block },
        [NMC_NODE_SECTION] = { NAME("section"), indenting_block },
        [NMC_NODE_PARAGRAPH] = { NAME("p"), block },
        [NMC_NODE_ITEMIZATION] = { NAME("itemization"), indenting_block },
        [NMC_NODE_ENUMERATION] = { NAME("enumeration"), indenting_block },
        [NMC_NODE_ITEM] = { NAME("item"), indenting_block },
        [NMC_NODE_DEFINITIONS] = { NAME("definitions"), indenting_block },
        [NMC_NODE_TERM] = { NAME("term"), text_block },
        [NMC_NODE_DEFINITION] = { NAME("definition"), indenting_block },
        [NMC_NODE_QUOTE] = { NAME("quote"), indenting_block },
        [NMC_NODE_LINE] = { NAME("line"), block },
        [NMC_NODE_ATTRIBUTION] = { NAME("attribution"), block },
        [NMC_NODE_CODEBLOCK] = { NAME("code"), text_block },
        [NMC_NODE_TABLE] = { NAME("table"), indenting_block },
        [NMC_NODE_HEAD] = { NAME("head"), indenting_block },
        [NMC_NODE_BODY] = { NAME("body"), indenting_block },
        [NMC_NODE_ROW] = { NAME("row"), indenting_block },
        [NMC_NODE_CELL] = { NAME("cell"), block },
        [NMC_NODE_FIGURE] = { NAME("figure"), indenting_block },
        [NMC_NODE_IMAGE] = { NAME("image"), data_block },
        [NMC_NODE_CODE] = { NAME("code"), inline },
        [NMC_NODE_EMPHASIS] = { NAME("emphasis"), inline },
        [NMC_NODE_GROUP] = { NULL, 0, (xmltraversefn)nmc_node_traverse_null, (xmltraversefn)nmc_node_traverse_null },
        [NMC_NODE_ABBREVIATION] = { NAME("abbreviation"), data },
        [NMC_NODE_REFERENCE] = { NAME("ref"), data },
        [NMC_NODE_TEXT] = { NULL, 0, text_enter, NULL },
#undef NAME
#undef indenting_block
#undef text_block
#undef block
#undef inline
#undef data
};

static bool
inline_enter(struct nmc_node *node, struct xml_closure *closure)
{
        return element_start(closure, types[node->name].name,
                             types[node->name].length) &&
                text_enter(node, closure);
}

static inline bool
outattributes(struct xml_closure *closure, struct nmc_node_datum *data)
{
        for (struct nmc_node_datum *p = data; p->name != NULL; p++) {
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
data_enter(struct nmc_data_node *node, struct xml_closure *closure)
{
        return outc(closure, '<') &&
                outs(closure, types[node->node.node.name].name,
                     types[node->node.node.name].length) &&
                outattributes(closure, node->data->data) &&
                outc(closure, '>');
}

static bool
block_enter(struct nmc_node *node, struct xml_closure *closure)
{
        return indent(closure, closure->indent) &&
                element_start(closure, types[node->name].name,
                              types[node->name].length);
}

static bool
leave(struct nmc_node *node, struct xml_closure *closure)
{
        return element_end(closure, types[node->name].name,
                           types[node->name].length);
}

static bool
xml_enter(struct nmc_node *node, struct xml_closure *closure)
{
        return types[node->name].enter(node, closure);
}

static bool
xml_leave(struct nmc_node *node, struct xml_closure *closure)
{
        return types[node->name].leave(node, closure);
}

bool
nmc_node_xml(struct nmc_node *node, struct nmc_output *output, struct nmc_error *error)
{
        struct xml_closure closure = { output, 0, error };
        static char xml_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        return outs(&closure, xml_header, sizeof(xml_header) - 1) &&
                nmc_node_traverse(node, (nmc_node_traverse_fn)xml_enter,
                                  (nmc_node_traverse_fn)xml_leave, &closure,
                                  error) &&
                outc(&closure, '\n');
}
