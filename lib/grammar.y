%code requires
{
#define YYLTYPE struct nmc_location
struct nmc_parser;

#include <stddef.h>

struct substring {
        const char *string;
        size_t length;
};

struct nodes {
        struct node *first;
        struct node *last;
};
}

%code provides
{
struct anchors;

void anchors_free(struct anchors *anchors);
}

%code
{
#include <sys/types.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <private.h>
#include "ext.h"
#include <nmc.h>
#include <nmc/list.h>
#include "parser.h"
#include "string.h"

struct anchor {
        struct anchor *next;
        struct nmc_location location;
        char *id;
        struct anchor_node *node;
};

static void
anchor_free1(struct anchor *anchor)
{
        free(anchor->id);
        /* NOTE anchor->node is either on the stack or in the tree, so we don’t
         * need to free it here. */
        nmc_free(anchor);
}

struct anchor_node {
        struct parent_node node;
        union {
                struct {
                        const char *name;
                        struct auxiliary_node_attribute *attributes;
                } auxiliary;
                struct anchor *anchor;
        } u;
};

struct anchors {
        struct anchors *next;
        char *id;
        struct anchor *anchors;
};

static void
anchors_free1(struct anchors *anchors)
{
        free(anchors->id);
        nmc_free(anchors);
}

void
anchors_free(struct anchors *anchors)
{
        list_for_each(struct anchors, p, anchors)
                anchors_free1(anchors);
}

static struct anchors *
anchors_find_prim(struct anchors *anchors, const char *id,
                  struct anchors **previous, struct anchors **next)
{
        *previous = *next = NULL;
        struct anchors *p = NULL;
        list_for_each_safe(struct anchors, c, n, anchors) {
                if (strcmp(c->id, id) == 0) {
                        *previous = p;
                        *next = n;
                        return c;
                }
                p = c;
        }
        return NULL;
}

static struct anchors *
anchors_find(struct anchors *anchors, const char *id)
{
        struct anchors *p, *n;
        return anchors_find_prim(anchors, id, &p, &n);
}

static struct anchor *
anchors_remove(struct anchors **anchors, const char *id)
{
        struct anchors *p, *n, *c = anchors_find_prim(*anchors, id, &p, &n);
        if (c == NULL)
                return NULL;
        if (p == NULL)
                *anchors = n;
        else
                p->next = n;
        struct anchor *a = c->anchors;
        anchors_free1(c);
        return a;
}

static struct anchors *
anchors_cons(struct anchors *anchors, struct anchor *anchor)
{
        list_for_each(struct anchors, p, anchors) {
                if (strcmp(p->id, anchor->id) == 0) {
                        anchor->next = p->anchors;
                        p->anchors = anchor;
                        return anchors;
                }
        }
        struct anchors *a = nmc_new(struct anchors);
        a->next = anchors;
        a->id = strdup(anchor->id);
        a->anchors = anchor;
        return a;
}

static void
anchor_unlink(struct node *node, struct nmc_parser *parser)
{
        if (node->name != NODE_ANCHOR)
                return;
        struct anchors *anchors = anchors_find(parser->anchors, ((struct anchor_node *)node)->u.anchor->id);
        struct anchor *p = NULL;
        list_for_each_safe(struct anchor, c, n, anchors->anchors) {
                if (c->node == (struct anchor_node *)node) {
                        if (p == NULL)
                                anchors->anchors = n;
                        else
                                p->next = n;
                        break;
                }
                p = c;
        }
}

static void
node_unlink_and_free(struct nmc_parser *parser, struct node *node)
{
        nmc_node_traverse(node, (traversefn)anchor_unlink, nmc_node_traverse_null, parser);
        /* TODO Once nmc_node_traverse can actually handle reporting OOM, if
         * that occurs, parser->anchors must be completely cleared. */
        node_free(node);
}

struct buffer_node {
        struct node node;
        union {
                char *text;
                struct nmc_string *buffer;
        } u;
};

typedef struct auxiliary_node *(*definefn)(const char *, regmatch_t *);

struct definition {
        struct definition *next;
        regex_t regex;
        definefn define;
};

static struct definition *definitions;

static void
definitions_push(const char *pattern, definefn define)
{
        struct definition *definition = nmc_new(struct definition);
        /* TODO Error-check here */
        regcomp(&definition->regex, pattern, REG_EXTENDED);
        definition->define = define;

        definitions = list_cons(definition, definitions);
}

static inline struct node *
node_init(struct node *node, enum node_type type, enum node_name name)
{
        node->next = NULL;
        node->type = type;
        node->name = name;
        return node;
}
#define node_new(stype, type, name) ((stype *)node_init((struct node *)nmc_new(stype), type, name))

static struct auxiliary_node *
definition_element(const char *name, const char *buffer, regmatch_t *matches, const char **attributes)
{
        int n = 0;
        for (const char **p = attributes; *p != NULL; p++)
                n++;
        struct auxiliary_node *d = node_new(struct auxiliary_node, AUXILIARY, NODE_AUXILIARY);
        d->node.children = NULL;
        d->name = name;
        d->attributes = nmc_new_n(struct auxiliary_node_attribute, n + 1);
        regmatch_t *m = &matches[1];
        struct auxiliary_node_attribute *a = d->attributes;
        for (const char **p = attributes; *p != NULL; p++) {
                /* TODO Check that rm_so/rm_eo ≠ -1 */
                a->name = *p;
                a->value = strndup(buffer + m->rm_so, m->rm_eo - m->rm_so);
                m++;
                a++;
        }
        a->name = NULL;
        a->value = NULL;
        return d;
}

static struct auxiliary_node *
abbreviation(const char *buffer, regmatch_t *matches)
{
        return definition_element("abbreviation", buffer, matches,
                                  (const char *[]){ "for", NULL });
}

static struct auxiliary_node *
ref(const char *buffer, regmatch_t *matches)
{
        return definition_element("ref", buffer, matches,
                                  (const char *[]){ "title", "uri", NULL });
}

void
nmc_grammar_initialize(void)
{
        if (definitions != NULL)
                return;
        definitions_push("^(.+) +at +(.+)", ref);
        definitions_push("^Abbreviation +for +(.+)", abbreviation);
}

void
nmc_grammar_finalize(void)
{
        list_for_each_safe(struct definition, p, n, definitions) {
                regfree(&p->regex);
                nmc_free(p);
        }
        definitions = NULL;
}

static struct auxiliary_node *
define(const char *content)
{
        list_for_each(struct definition, p, definitions) {
                regmatch_t matches[p->regex.re_nsub + 1];

                if (regexec(&p->regex, content,
                            p->regex.re_nsub + 1, matches, 0) == 0)
                        return p->define(content, matches);
        }

        return NULL;
}

static struct node *
parent_node_free(struct parent_node *node)
{
        return node->children;
}

static struct node *
auxiliary_node_free(struct auxiliary_node *node)
{
        for (struct auxiliary_node_attribute *a = node->attributes; a->name != NULL; a++)
                nmc_free(a->value);
        nmc_free(node->attributes);
        return parent_node_free((struct parent_node *)node);
}

static struct node *
text_node_free(struct text_node *node)
{
        nmc_free(node->text);
        return NULL;
}

static struct node *
private_node_free(struct node *node)
{
        switch (node->name) {
        case NODE_BUFFER:
                nmc_string_free(((struct buffer_node *)node)->u.buffer);
                return NULL;
        case NODE_ANCHOR:
                anchor_free1(((struct anchor_node *)node)->u.anchor);
                return parent_node_free((struct parent_node *)node);
        default:
                /* TODO assert(false); */
                return NULL;
        }
}

void
node_free(struct node *node)
{
        typedef struct node *(*nodefreefn)(struct node *);
        static nodefreefn fns[] = {
                [PARENT] = (nodefreefn)parent_node_free,
                [AUXILIARY] = (nodefreefn)auxiliary_node_free,
                [TEXT] = (nodefreefn)text_node_free,
                [PRIVATE] = (nodefreefn)private_node_free,
        };

        if (node == NULL)
                return;
        struct node *p = node;
        struct node *last = p;
        while (last->next != NULL)
                last = last->next;
        while (p != NULL) {
                struct node *children = fns[p->type](p);
                if (children != NULL) {
                        last->next = children;
                        while (last->next != NULL)
                                last = last->next;
                }
                struct node *next = p->next;
                nmc_free(p);
                p = next;
        }
}

char *
nmc_location_str(const struct nmc_location *l)
{
        char *s;
        if (l->first_line == l->last_line) {
                if (l->first_column == l->last_column)
                        nmc_asprintf(&s, "%d:%d", l->first_line, l->first_column);
                else
                        nmc_asprintf(&s, "%d.%d-%d", l->first_line, l->first_column, l->last_column);
        } else
                nmc_asprintf(&s, "%d.%d-%d.%d", l->first_line, l->first_column, l->last_line, l->last_column);
        return s;
}

struct footnote {
        struct footnote *next;
        YYLTYPE location;
        char *id;
        struct auxiliary_node *node;
};

static struct footnote *
footnote_new(struct nmc_parser *parser, YYLTYPE *location, char *id, struct nmc_string *string)
{
        struct footnote *footnote = nmc_new(struct footnote);
        footnote->next = NULL;
        footnote->location = *location;
        footnote->id = id;
        footnote->node = define(nmc_string_str(string));
        if (footnote->node == NULL)
                nmc_parser_error(parser, location,
                                 "unrecognized footnote content: %s",
                                 nmc_string_str(string));
        nmc_string_free(string);
        return footnote;
}

static void
footnote_free(struct footnote *footnote)
{
        free(footnote->id);
        node_free((struct node *)footnote->node);
        nmc_free(footnote);
}

struct sigil {
        struct sigil *next;
        YYLTYPE location;
        char *id;
};

static struct sigil *
sigil_new(YYLTYPE *location, const char *string, int length)
{
        struct sigil *sigil = nmc_new(struct sigil);
        sigil->next = NULL;
        sigil->location = *location;
        sigil->id = strndup(string, length);
        return sigil;
}

static void
sigil_free1(struct sigil *sigil)
{
        free(sigil->id);
        nmc_free(sigil);
}

static void
sigil_free(struct sigil *sigil)
{
        list_for_each_safe(struct sigil, p, n, sigil)
                sigil_free1(p);
}

static void
report_remaining_anchors(struct nmc_parser *parser)
{
        struct nmc_parser_error *first = NULL, *previous = NULL, *last = NULL;
        /* TODO Sort these by location */
        list_for_each_safe(struct anchors, p, n, parser->anchors) {
                list_for_each(struct anchor, q, p->anchors) {
                        first = nmc_parser_error_new(&q->location,
                                                     "reference to undefined footnote: %s",
                                                     q->id);
                        if (last == NULL)
                                last = first;
                        if (previous != NULL)
                                first->next = previous;
                        previous = first;
                }
                anchors_free1(p);
        }
        parser->anchors = NULL;
        nmc_parser_errors(parser, first, last);
}
}

%define api.pure
%parse-param {struct nmc_parser *parser}
%lex-param {struct nmc_parser *parser}
%name-prefix "nmc_grammar_"
%debug
%error-verbose
%expect 0
%locations

%token END 0 "end of file"
%token ERROR
%token AGAIN
%token <string> TITLE
%token <substring> WORD
%token PARAGRAPH
%token <substring> SPACE
%token CONTINUATION
%token <substring> BLOCKSEPARATOR
%token ITEMIZATION
%token ENUMERATION
%token <substring> DEFINITION
%token QUOTE
%token ATTRIBUTION
%token TABLESEPARATOR
%token ROW
%token ENTRYSEPARATOR "entry separator"
%token <string> CODEBLOCK
%token <raw_footnote> FOOTNOTE
%token SECTION
%token INDENT
%token DEDENT
%token <substring> CODE
%token <substring> EMPHASIS
%token <substring> SIGIL
%token SIGILSEPARATOR
%token BEGINGROUP
%token ENDGROUP

%type <nodes> oblockssections0 blockssections blocks sections oblockssections
%type <node> block footnotedsection section title
%type <footnote> footnotes footnote
%type <node> paragraph
%type <node> itemization itemizationitem
%type <nodes> itemizationitems
%type <node> enumeration enumerationitem
%type <nodes> enumerationitems
%type <node> definitions definition
%type <nodes> definitionitems
%type <node> quote line attribution
%type <nodes> lines
%type <node> table row entry
%type <nodes> headbody body entries
%type <nodes> inlines sinlines
%type <node> anchoredinline inline
%type <sigil> sigils sigil
%type <node> item
%type <nodes> oblocks

%union {
        struct substring substring;
        struct {
                char *id;
                struct nmc_string *string;
        } raw_footnote;
        struct nmc_string *string;
        struct nodes nodes;
        struct node *node;
        struct footnote *footnote;
        struct sigil *sigil;
}

%printer { fprintf(yyoutput, "%.*s", (int)$$.length, $$.string); } <substring>
%printer { fprintf(yyoutput, "%s %s", $$.id, nmc_string_str($$.string)); } <raw_footnote>
%printer { fprintf(yyoutput, "%s", nmc_string_str($$)); } <string>

%destructor { free($$.id); nmc_string_free($$.string); } <raw_footnote>
%destructor { nmc_string_free($$); } <string>
%destructor { node_unlink_and_free(parser, $$.first); } <nodes>
%destructor { node_unlink_and_free(parser, $$); } <node>
%destructor { footnote_free($$); } <footnote>
%destructor { sigil_free($$); } <sigil>

%code
{
static int
nmc_grammar_lex(YYSTYPE *value, YYLTYPE *location, struct nmc_parser *parser)
{
        int token;

        do {
                token = nmc_parser_lex(parser, location, value);
        } while (token == AGAIN);

        return token;
}

static void
nmc_grammar_error(YYLTYPE *location, struct nmc_parser *parser, const char *message)
{
        nmc_parser_error(parser, location, "%s", message);
}

static struct node *
text_node(enum node_name name, char *text)
{
        struct text_node *n = node_new(struct text_node, TEXT, name);
        n->text = text;
        return (struct node *)n;
}

static struct node *
text(enum node_name name, struct nmc_string *string)
{
        return text_node(name, nmc_string_str_free(string));
}

static struct node *
subtext(enum node_name name, struct substring substring)
{
        return text_node(name, strndup(substring.string, substring.length));
}

static inline struct nodes
nodes(struct node *node)
{
        return (struct nodes){ node, node };
}

static inline struct nodes
sibling(struct nodes siblings, struct node *sibling)
{
        siblings.last->next = sibling;
        return (struct nodes){ siblings.first, sibling };
}

static inline struct nodes
siblings(struct nodes siblings, struct nodes rest)
{
        siblings.last->next = rest.first;
        return (struct nodes){ siblings.first, rest.last };
}

static inline struct nodes
nibling(struct node *sibling, struct nodes siblings)
{
        sibling->next = siblings.first;
        return (struct nodes){ sibling, siblings.last };
}

static inline struct node *
parent1(enum node_name name, struct node *children)
{
        struct parent_node *n = node_new(struct parent_node, PARENT, name);
        n->children = children;
        return (struct node *)n;
}

static inline struct node *
parent(enum node_name name, struct nodes children)
{
        return parent1(name, children.first);
}

static inline struct node *
parent_children(enum node_name name, struct node *first, struct nodes rest)
{
        first->next = rest.first;
        return parent1(name, first);
}

static void
update_anchors(struct anchor *anchors, struct auxiliary_node *node)
{
        list_for_each_safe(struct anchor, p, n, anchors) {
                p->node->node.node.type = node->node.node.type;
                p->node->node.node.name = node->node.node.name;
                p->node->u.auxiliary.name = node->name;
                p->node->u.auxiliary.attributes = node->attributes;
                p->node = NULL;
                anchor_free1(p);
        }
}

static void
footnote(struct nmc_parser *parser, struct footnote *footnotes)
{
        list_for_each_safe(struct footnote, p, n, footnotes) {
                struct anchor *anchors = anchors_remove(&parser->anchors, p->id);
                if (anchors == NULL) {
                        nmc_parser_error(parser,
                                         &p->location,
                                         "unreferenced footnote: %s", p->id);
                        continue;
                }

                if (p->node != NULL) {
                        update_anchors(anchors, p->node);
                        p->node = NULL;
                }
                footnote_free(p);
        }
}
#define footnote(parser, result, footnotes) (footnote(parser, footnotes), result)

static inline struct footnote *
fibling(struct nmc_parser *parser, struct footnote *footnotes, struct footnote *footnote)
{
        struct footnote *last;
        list_for_each(struct footnote, p, footnotes) {
                if (strcmp(footnote->id, p->id) == 0) {
                        char *s = nmc_location_str(&p->location);
                        nmc_parser_error(parser, &footnote->location,
                                         "footnote already defined at %s: %s", s, p->id);
                        free(s);
                        return footnotes;
                }
                last = p;
        }
        last->next = footnote;
        return footnotes;
}

static struct node *
definition(struct substring substring, struct node *item)
{
        struct node *n = subtext(NODE_TERM, substring);
        n->next = parent1(NODE_DEFINITION, ((struct parent_node *)item)->children);
        ((struct parent_node *)item)->children = n;
        return item;
}

/* Anchor sigils appearing in reverse order. */
static struct node *
anchor(struct nmc_parser *parser, struct node *atom, struct sigil *sigils)
{
        struct node *outermost = atom;
        list_for_each(struct sigil, p, sigils) {
                struct anchor *anchor = nmc_new(struct anchor);
                anchor->next = NULL;
                anchor->location = p->location;
                anchor->id = strdup(p->id);
                anchor->node = node_new(struct anchor_node, PRIVATE, NODE_ANCHOR);
                anchor->node->u.anchor = anchor;
                if (outermost == atom) {
                        anchor->node->node.children = atom;
                        outermost = (struct node *)anchor->node;
                } else {
                        anchor->node->node.children = ((struct parent_node *)outermost)->children;
                        ((struct parent_node *)outermost)->children = (struct node *)anchor->node;
                }

                parser->anchors = anchors_cons(parser->anchors, anchor);

                sigil_free1(p);
        }
        return outermost;
}

static struct node *
buffer(struct substring substring)
{
        struct buffer_node *n = node_new(struct buffer_node, PRIVATE, NODE_BUFFER);
        n->u.buffer = nmc_string_new(substring.string, substring.length);
        return (struct node *)n;
}

static inline struct node *
word(struct nmc_parser *parser, struct substring substring, struct sigil *sigils)
{
        return (sigils != NULL) ?
                anchor(parser, subtext(NODE_TEXT, substring), sigils) :
                buffer(substring);
}

static inline struct nodes
append_text(struct nodes inlines, struct substring substring)
{
        if (inlines.last->name != NODE_BUFFER)
                return sibling(inlines, buffer(substring));

        nmc_string_append(((struct buffer_node *)inlines.last)->u.buffer, substring.string, substring.length);
        return inlines;
}

static inline struct nodes
append_space(struct nodes inlines)
{
        return append_text(inlines, (struct substring){ " ", 1 });
}

static inline struct nodes
textify(struct nodes inlines)
{
        if (inlines.last->name == NODE_BUFFER) {
                struct buffer_node *buffer = (struct buffer_node *)inlines.last;
                buffer->node.type = TEXT;
                buffer->node.name = NODE_TEXT;
                buffer->u.text = nmc_string_str_free(buffer->u.buffer);
        }
        return inlines;
}

static inline struct nodes
inline_sibling(struct nodes inlines, struct node *node)
{
        return sibling(textify(inlines), node);
}

static inline struct nodes
append_inline(struct nodes inlines, struct node *node)
{
        return inline_sibling(append_space(inlines), node);
}

static inline struct nodes
append_word(struct nmc_parser *parser, struct nodes inlines, struct substring substring, struct sigil *sigils)
{
        return (sigils != NULL) ?
                inline_sibling(inlines, word(parser, substring, sigils)) :
                append_text(inlines, substring);
}

static inline struct nodes
append_spaced_word(struct nmc_parser *parser, struct nodes inlines, struct substring substring, struct sigil *sigils)
{
        return (sigils != NULL) ?
                append_inline(inlines, word(parser, substring, sigils)) :
                append_text(append_space(inlines), substring);
}
}

%%

nmc: TITLE oblockssections0 {
        parser->doc = parent_children(NODE_DOCUMENT, parent1(NODE_TITLE, text(NODE_TEXT, $1)), $2);
        report_remaining_anchors(parser);
};

oblockssections0: /* empty */ { $$ = nodes(NULL); }
| BLOCKSEPARATOR blockssections { $$ = $2; };

blockssections: blocks
| blocks BLOCKSEPARATOR sections { $$ = siblings($1, $3); }
| sections;

blocks: block { $$ = nodes($1); }
| blocks BLOCKSEPARATOR block { $$ = sibling($1, $3); }
| blocks BLOCKSEPARATOR footnotes { $$ = footnote(parser, $1, $3); };

block: paragraph
| itemization
| enumeration
| definitions
| quote
| CODEBLOCK { $$ = text(NODE_CODEBLOCK, $1); }
| table;

sections: footnotedsection { $$ = nodes($1); }
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes oblockseparator { $$ = footnote(parser, $1, $2); };

oblockseparator: /* empty */
| BLOCKSEPARATOR;

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = parent_children(NODE_SECTION, $3, $4); };

title: inlines { $$ = parent(NODE_TITLE, $1); };

oblockssections: /* empty */ { $$ = nodes(NULL); }
| INDENT blockssections DEDENT { $$ = $2; };

footnotes: footnote
| footnotes footnote { $$ = fibling(parser, $1, $2); };

footnote: FOOTNOTE { $$ = footnote_new(parser, &@$, $1.id, $1.string); };

paragraph: PARAGRAPH inlines { $$ = parent(NODE_PARAGRAPH, $2); };

itemization: itemizationitems { $$ = parent(NODE_ITEMIZATION, $1); };

itemizationitems: itemizationitem { $$ = nodes($1); }
| itemizationitems itemizationitem { $$ = sibling($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

enumeration: enumerationitems { $$ = parent(NODE_ENUMERATION, $1); };

enumerationitems: enumerationitem { $$ = nodes($1); }
| enumerationitems enumerationitem { $$ = sibling($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

definitions: definitionitems { $$ = parent(NODE_DEFINITIONS, $1); };

definitionitems: definition { $$ = nodes($1); }
| definitionitems definition { $$ = sibling($1, $2); };

definition: DEFINITION item { $$ = definition($1, $2); };

quote: lines attribution { $$ = parent(NODE_QUOTE, sibling($1, $2)); };

lines: line { $$ = nodes($1); }
| lines line { $$ = sibling($1, $2); };

line: QUOTE inlines { $$ = parent(NODE_LINE, $2); };

attribution: /* empty */ { $$ = NULL; }
| ATTRIBUTION inlines { $$ = parent(NODE_ATTRIBUTION, $2); };

table: headbody { $$ = parent(NODE_TABLE, $1); }

headbody: row { $$ = nodes(parent1(NODE_BODY, $1)); }
| row TABLESEPARATOR body { $$ = sibling(nodes(parent1(NODE_HEAD, $1)), parent(NODE_BODY, $3)); }
| row body { $$ = nodes(parent(NODE_BODY, sibling(nodes($1), $2.first))); };

body: row { $$ = nodes($1); }
| body row { $$ = sibling($1, $2); };

row: ROW entries ENTRYSEPARATOR { $$ = parent(NODE_ROW, $2); };

entries: entry { $$ = nodes($1); }
| entries ENTRYSEPARATOR entry { $$ = sibling($1, $3); };

entry: inlines { $$ = parent(NODE_ENTRY, $1); };

inlines: ospace sinlines ospace { $$ = textify($2); };

sinlines: WORD sigils { $$ = nodes(word(parser, $1, $2)); }
| anchoredinline { $$ = nodes($1); }
| sinlines WORD sigils { $$ = append_word(parser, $1, $2, $3); }
| sinlines anchoredinline { $$ = sibling($1, $2); }
| sinlines spaces WORD sigils { $$ = append_spaced_word(parser, $1, $3, $4); }
| sinlines spaces anchoredinline { $$ = append_inline($1, $3); };

anchoredinline: inline sigils { $$ = anchor(parser, $1, $2); };

inline: CODE { $$ = subtext(NODE_CODE, $1); }
| EMPHASIS { $$ = subtext(NODE_EMPHASIS, $1); }
| BEGINGROUP sinlines ENDGROUP { $$ = parent(NODE_GROUP, textify($2)); };

sigils: /* empty */ { $$ = NULL; }
| sigil
| sigils SIGILSEPARATOR sigil { $$ = $3; $$->next = $1; };

sigil: SIGIL { $$ = sigil_new(&@$, $1.string, $1.length); };

ospace: /* empty */
| SPACE;

spaces: spacecontinuation
| spaces spacecontinuation;

spacecontinuation: SPACE
| CONTINUATION;

item: { parser->want = INDENT; } inlines oblocks { $$ = parent_children(NODE_ITEM, parent(NODE_PARAGRAPH, $2), $3); };

oblocks: /* empty */ { $$ = nodes(NULL); }
| INDENT blocks DEDENT { $$ = $2; };
