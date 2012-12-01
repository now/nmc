#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <private.h>
#include "grammar.h"
#include <nmc.h>
#include <nmc/list.h>

enum node_level
{
        ILLEGAL,
        INLINE,
        BLOCK,
        INDENTING_BLOCK
};

static struct {
        const char *name;
        enum node_level level;
} types[] = {
        [NODE_DOCUMENT] = { "nml", INDENTING_BLOCK },
        [NODE_TITLE] = { "title", BLOCK },
        [NODE_PARAGRAPH] = { "p", BLOCK },
        [NODE_ITEMIZATION] = { "itemization", INDENTING_BLOCK },
        [NODE_ENUMERATION] = { "enumeration", INDENTING_BLOCK },
        [NODE_DEFINITIONS] = { "definitions", INDENTING_BLOCK },
        [NODE_DEFINITION] = { "definition", INDENTING_BLOCK },
        [NODE_TERM] = { "term", BLOCK },
        [NODE_ITEM] = { "item", INDENTING_BLOCK },
        [NODE_QUOTE] = { "quote", INDENTING_BLOCK },
        [NODE_LINE] = { "line", BLOCK },
        [NODE_ATTRIBUTION] = { "attribution", BLOCK },
        [NODE_CODEBLOCK] = { "code", BLOCK },
        [NODE_TABLE] = { "table", INDENTING_BLOCK },
        [NODE_HEAD] = { "head", INDENTING_BLOCK },
        [NODE_BODY] = { "body", INDENTING_BLOCK },
        [NODE_ROW] = { "row", INDENTING_BLOCK },
        [NODE_ENTRY] = { "entry", BLOCK },
        [NODE_SECTION] = { "section", INDENTING_BLOCK },
        [NODE_CODE] = { "code", INLINE },
        [NODE_EMPHASIS] = { "emphasis", INLINE },
        [NODE_GROUP] = { NULL, INLINE },
        [NODE_AUXILIARY] = { NULL, INLINE },
        [NODE_TEXT] = { NULL, INLINE },
        [NODE_BUFFER] = { NULL, ILLEGAL },
        [NODE_ANCHOR] = { NULL, ILLEGAL },
};

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
                n = nmc_new(struct action);
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
                nmc_free(p);
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

struct xml_closure
{
        int indent;
};

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
element(struct node *node, bool enter)
{
        if (node->name == NODE_GROUP)
                return;

        struct auxiliary_node *a = (node->name == NODE_AUXILIARY) ?
                (struct auxiliary_node *)node : NULL;

        putchar('<');
        if (!enter)
                putchar('/');
        fputs(a != NULL ? a->name : types[node->name].name, stdout);
        if (enter && a != NULL)
                for (struct auxiliary_node_attribute *p = a->attributes; p->name != NULL; p++) {
                        putchar(' ');
                        fputs(p->name, stdout);
                        putchar('=');
                        putchar('"');
                        escape(p->value, nmc_lengthof(attribute_entities), attribute_entities);
                        putchar('"');
                }
        putchar('>');
}

static void
xml_enter(struct node *node, struct xml_closure *closure)
{
        if (types[node->name].level != INLINE)
                indent(closure->indent);

        if (node->name == NODE_TEXT) {
        text:
                escape(((struct text_node *)node)->text, nmc_lengthof(text_entities), text_entities);
                return;
        }

        element(node, true);

        if (node->type == TEXT)
                goto text;

        if (types[node->name].level == INDENTING_BLOCK)
                closure->indent++;
}

static void
xml_leave(struct node *node, struct xml_closure *closure)
{
        if (types[node->name].level == INDENTING_BLOCK) {
                closure->indent--;
                indent(closure->indent);
        }

        element(node, false);
}

void
nmc_node_to_xml(struct node *node)
{
        struct xml_closure closure = { 0 };
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", stdout);
        nmc_node_traverse(node, (traversefn)xml_enter, (traversefn)xml_leave, &closure);
        putchar('\n');
}
