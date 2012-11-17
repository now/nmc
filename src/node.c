#include <stdbool.h>
#include <stdio.h>
#include <libxml/tree.h>
#include "list.h"
#include "nmc.h"
#include "node.h"

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
        bool text;
        bool nested;
} types[] = {
        [NODE_DOCUMENT] = { "nml", INDENTING_BLOCK, false, true },
        [NODE_TITLE] = { "title", BLOCK, false, true },
        [NODE_PARAGRAPH] = { "p", BLOCK, false, true },
        [NODE_ITEMIZATION] = { "itemization", INDENTING_BLOCK, false, true },
        [NODE_ENUMERATION] = { "enumeration", INDENTING_BLOCK, false, true },
        [NODE_DEFINITIONS] = { "definitions", INDENTING_BLOCK, false, true },
        [NODE_DEFINITION] = { "definition", INDENTING_BLOCK, false, true },
        [NODE_TERM] = { "term", BLOCK, true, true },
        [NODE_ITEM] = { "item", INDENTING_BLOCK, false, true },
        [NODE_QUOTE] = { "quote", INDENTING_BLOCK, false, true },
        [NODE_LINE] = { "line", BLOCK, false, true },
        [NODE_ATTRIBUTION] = { "attribution", BLOCK, false, true },
        [NODE_CODEBLOCK] = { "code", BLOCK, true, true },
        [NODE_TABLE] = { "table", INDENTING_BLOCK, false, true },
        [NODE_HEAD] = { "head", INDENTING_BLOCK, false, true },
        [NODE_BODY] = { "body", INDENTING_BLOCK, false, true },
        [NODE_ROW] = { "row", INDENTING_BLOCK, false, true },
        [NODE_ENTRY] = { "entry", BLOCK, false, true },
        [NODE_SECTION] = { "section", INDENTING_BLOCK, false, true },
        [NODE_CODE] = { "code", INLINE, true, true },
        [NODE_EMPHASIS] = { "emphasis", INLINE, true, true },
        [NODE_GROUP] = { NULL, INLINE, false, true },
        [NODE_BUFFER] = { NULL, ILLEGAL, false, false },
        [NODE_TEXT] = { NULL, INLINE, true, false },
        [NODE_ANCHOR] = { NULL, ILLEGAL, false, false },
        [NODE_AUXILIARY] = { NULL, INLINE, false, true }
};

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

                        if (types[n->type].nested) {
                                actions = action_new(&used, actions, false, n);
                                if (!types[n->type].text)
                                        actions = action_new(&used, actions, true, n->u.children);
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
                if (types[p->type].nested) {
                        if (!types[p->type].text)
                                nmc_node_traverse_r(p->u.children, enter, leave, closure);
                        leave(p, closure);
                }
        }
}

void
node_free(struct node *node)
{
        if (node == NULL)
                return;
        struct node *p = node;
        struct node *last = p;
        while (last->next != NULL)
                last = last->next;
        while (p != NULL) {
                /* TODO Add free function to types? */
                switch (p->type) {
                /* TODO Move this to grammar.y? */
                case NODE_BUFFER:
                        xmlBufferFree(p->u.buffer);
                        break;
                case NODE_TEXT:
                        nmc_free(p->u.text);
                        break;
                case NODE_AUXILIARY: {
                        struct auxiliary_node *as = (struct auxiliary_node *)p;
                        for (struct auxiliary_node_attribute *a = as->attributes; a->name != NULL; a++)
                                nmc_free(a->value);
                        nmc_free(as->attributes);
                }
                        /* fall through */
                default:
                        /* TODO Gheesh, add free function to types already. */
                        if (!types[p->type].text) {
                                last->next = p->u.children;
                                while (last->next != NULL)
                                        last = last->next;
                        } else
                                nmc_free(p->u.text);
                        break;
                }
                struct node *next = p->next;
                nmc_free(p);
                p = next;
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
        if (node->type == NODE_GROUP)
                return;

        struct auxiliary_node *a = (node->type == NODE_AUXILIARY) ?
                (struct auxiliary_node *)node : NULL;

        putchar('<');
        if (!enter)
                putchar('/');
        fputs(a != NULL ? a->name : types[node->type].name, stdout);
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
        if (types[node->type].level != INLINE)
                indent(closure->indent);

        if (node->type == NODE_TEXT) {
        text:
                escape(node->u.text, nmc_lengthof(text_entities), text_entities);
                return;
        }

        element(node, true);

        if (types[node->type].text)
                goto text;

        if (types[node->type].level == INDENTING_BLOCK)
                closure->indent++;
}

static void
xml_leave(struct node *node, struct xml_closure *closure)
{
        if (types[node->type].level == INDENTING_BLOCK) {
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
