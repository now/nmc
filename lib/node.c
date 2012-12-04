#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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

static struct action *
action_new(struct action **used, struct action *next, bool enter, struct node *nodes)
{
        struct action *n;
        if (*used != NULL) {
                n = *used;
                *used = (*used)->next;
        } else
                n = malloc(sizeof(struct action));
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

void
nmc_node_traverse(struct node *node, traversefn enter, traversefn leave, void *closure)
{
        if (node == NULL)
                return;
        struct action *used = NULL;
        struct action *actions = action_new(&used, NULL, true, node);
        while (actions != NULL) {
                if (actions->enter) {
                        struct node *n = actions->nodes;
                        actions->nodes = n->next;
                        if (actions->nodes == NULL)
                                move_to_used(&actions, &used);
                        enter(n, closure);

                        if (NODE_IS_NESTED(n)) {
                                actions = action_new(&used, actions, false, n);
                                if (NODE_HAS_CHILDREN(n))
                                        actions = action_new(&used, actions, true, ((struct parent_node *)n)->children);
                        }
                } else {
                        leave(actions->nodes, closure);
                        move_to_used(&actions, &used);
                }
        }
        list_for_each_safe(struct action, p, n, used)
                free(p);
}

void
nmc_node_traverse_r(struct node *node, traversefn enter, traversefn leave, void *closure)
{
        list_for_each(struct node, p, node) {
                enter(p, closure);
                if (NODE_IS_NESTED(p)) {
                        if (NODE_HAS_CHILDREN(p))
                                nmc_node_traverse_r(((struct parent_node *)p)->children, enter, leave, closure);
                        leave(p, closure);
                }
        }
}

static void
indent(int n)
{
        putchar('\n');
        for (int i = 0; i < n; i++) {
                putchar(' ');
                putchar(' ');
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
escape(const char *string, size_t n_entities, const char * const *entities)
{
        const char *s = string;
        const char *e = s;
        while (*e != '\0') {
                if ((unsigned)*e < n_entities && entities[(unsigned)*e] != NULL) {
                        fwrite(s, sizeof(char), e - s, stdout);
                        fputs(entities[(unsigned)*e], stdout);
                        s = e + 1;
                }
                e++;
        }
        fwrite(s, sizeof(char), e - s, stdout);
}

static void
element_start(const char *name)
{
        putchar('<');
        fputs(name, stdout);
        putchar('>');
}

static void
element_end(const char *name)
{
        putchar('<');
        putchar('/');
        fputs(name, stdout);
        putchar('>');
}

struct xml_closure {
        int indent;
};

static void
text_enter(struct node *node, UNUSED(struct xml_closure *closure))
{
        escape(((struct text_node *)node)->text, lengthof(text_entities), text_entities);
}

typedef void (*xmltraversefn)(struct node *, struct xml_closure *);

static struct xml_types {
        const char *name;
        xmltraversefn enter;
        xmltraversefn leave;
} types[];

static void
inline_enter(struct node *node, UNUSED(struct xml_closure *closure))
{
        element_start(types[node->name].name);
        text_enter(node, closure);
}

static void
block_enter(struct node *node, struct xml_closure *closure)
{
        indent(closure->indent);
        element_start(types[node->name].name);
}

static void
auxiliary_enter(struct auxiliary_node *node, UNUSED(struct xml_closure *closure))
{
        putchar('<');
        fputs(node->name, stdout);
        for (struct auxiliary_node_attribute *p = node->attributes->items; p->name != NULL; p++) {
                putchar(' ');
                fputs(p->name, stdout);
                putchar('=');
                putchar('"');
                escape(p->value, lengthof(attribute_entities), attribute_entities);
                putchar('"');
        }
        putchar('>');
}

static void
auxiliary_leave(struct auxiliary_node *node, UNUSED(struct xml_closure *closure))
{
        element_end(node->name);
}

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

static void
leave(struct node *node, UNUSED(struct xml_closure *closure))
{
        element_end(types[node->name].name);
}

static void
indenting_block_leave(struct node *node, struct xml_closure *closure)
{
        closure->indent--;
        indent(closure->indent);
        leave(node, closure);
}

static struct xml_types types[] = {
#define indenting_block indenting_block_enter, indenting_block_leave
#define text_block text_block_enter, leave
#define block block_enter, leave
#define inline inline_enter, leave
        [NODE_DOCUMENT] = { "nml", indenting_block },
        [NODE_TITLE] = { "title", text_block },
        [NODE_PARAGRAPH] = { "p", block },
        [NODE_ITEMIZATION] = { "itemization", indenting_block },
        [NODE_ENUMERATION] = { "enumeration", indenting_block },
        [NODE_DEFINITIONS] = { "definitions", indenting_block },
        [NODE_DEFINITION] = { "definition", indenting_block },
        [NODE_TERM] = { "term", text_block },
        [NODE_ITEM] = { "item", indenting_block },
        [NODE_QUOTE] = { "quote", indenting_block },
        [NODE_LINE] = { "line", block },
        [NODE_ATTRIBUTION] = { "attribution", block },
        [NODE_CODEBLOCK] = { "code", text_block },
        [NODE_TABLE] = { "table", indenting_block },
        [NODE_HEAD] = { "head", indenting_block },
        [NODE_BODY] = { "body", indenting_block },
        [NODE_ROW] = { "row", indenting_block },
        [NODE_ENTRY] = { "entry", block },
        [NODE_SECTION] = { "section", indenting_block },
        [NODE_CODE] = { "code", inline },
        [NODE_EMPHASIS] = { "emphasis", inline },
        [NODE_GROUP] = { NULL, (xmltraversefn)nmc_node_traverse_null, (xmltraversefn)nmc_node_traverse_null },
        [NODE_AUXILIARY] = { NULL, (xmltraversefn)auxiliary_enter, (xmltraversefn)auxiliary_leave },
        /* TODO Could have assertions for the NULLs here instead. */
        [NODE_TEXT] = { NULL, text_enter, NULL },
        [NODE_BUFFER] = { NULL, NULL, NULL },
        [NODE_ANCHOR] = { NULL, NULL, NULL },
#undef indenting_block
#undef text_block
#undef block
#undef inline
};

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

void
nmc_node_to_xml(struct node *node)
{
        struct xml_closure closure = { 0 };
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", stdout);
        nmc_node_traverse(node, (traversefn)xml_enter, (traversefn)xml_leave, &closure);
        putchar('\n');
}
