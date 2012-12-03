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
struct anchor;
void anchor_free(struct anchor *anchor);
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
#include "buffer.h"

struct id {
        unsigned long hash;
        char *string;
};

static struct id
id_new(char *string)
{
        unsigned long h = 5381;
        unsigned char *p = (unsigned char *)string;
        while (*p != '\0')
                h = 33 * h + *p++;
        return (struct id){ h, string };
}

static bool
id_eq(const struct id *a, const struct id *b)
{
        return a->hash == b->hash && strcmp(a->string, b->string) == 0;
}

struct anchor {
        struct anchor *next;
        struct nmc_location location;
        struct id id;
        struct anchor_node *node;
};

static void
anchor_free1(struct anchor *anchor)
{
        if (anchor == NULL)
                return;
        free(anchor->id.string);
        /* NOTE anchor->node is either on the stack or in the tree, so we don’t
         * need to free it here. */
        nmc_free(anchor);
}

void
anchor_free(struct anchor *anchor)
{
        list_for_each_safe(struct anchor, p, n, anchor)
                anchor_free1(p);
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

static void
anchor_unlink(struct node *node, struct nmc_parser *parser)
{
        if (node->name != NODE_ANCHOR)
                return;
        struct anchor *p = NULL;
        list_for_each_safe(struct anchor, c, n, parser->anchors) {
                if (c->node == (struct anchor_node *)node) {
                        if (p == NULL)
                                parser->anchors = n;
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
                struct buffer *buffer;
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
                buffer_free(((struct buffer_node *)node)->u.buffer);
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
        struct id id;
        struct auxiliary_node *node;
};

static struct footnote *
footnote_new(struct nmc_parser *parser, YYLTYPE *location, char *id, struct buffer *buffer)
{
        struct footnote *footnote = nmc_new(struct footnote);
        footnote->next = NULL;
        footnote->location = *location;
        footnote->id = id_new(id);
        footnote->node = define(buffer_str(buffer));
        if (footnote->node == NULL)
                nmc_parser_error(parser, location,
                                 "unrecognized footnote content: %s",
                                 buffer_str(buffer));
        buffer_free(buffer);
        return footnote;
}

static void
footnote_free(struct footnote *footnote)
{
        free(footnote->id.string);
        node_free((struct node *)footnote->node);
        nmc_free(footnote);
}

struct sigil {
        struct sigil *next;
        YYLTYPE location;
        char id[];
};

static struct sigil *
sigil_new(YYLTYPE *location, const char *string, size_t length)
{
        struct sigil *sigil = malloc(sizeof(struct sigil) + length + 1);
        sigil->next = NULL;
        sigil->location = *location;
        memcpy(sigil->id, string, length);
        sigil->id[length] = '\0';
        return sigil;
}

static void
sigil_free1(struct sigil *sigil)
{
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
        list_for_each_safe(struct anchor, p, n, parser->anchors) {
                first = nmc_parser_error_new(&p->location,
                                             "reference to undefined footnote: %s",
                                             p->id.string);
                if (last == NULL)
                        last = first;
                if (previous != NULL)
                        first->next = previous;
                previous = first;
                p->node->u.anchor = NULL;
                anchor_free1(p);
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
%token <buffer> TITLE
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
%token <buffer> CODEBLOCK
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
                struct buffer *buffer;
        } raw_footnote;
        struct buffer *buffer;
        struct nodes nodes;
        struct node *node;
        struct footnote *footnote;
        struct sigil *sigil;
}

%printer { fprintf(yyoutput, "%.*s", (int)$$.length, $$.string); } <substring>
%printer { fprintf(yyoutput, "%s %s", $$.id, buffer_str($$.buffer)); } <raw_footnote>
%printer { fprintf(yyoutput, "%s", buffer_str($$)); } <buffer>

%destructor { free($$.id); buffer_free($$.buffer); } <raw_footnote>
%destructor { buffer_free($$); } <buffer>
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
text(enum node_name name, struct buffer *buffer)
{
        return text_node(name, buffer_str_free(buffer));
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
update_anchors(struct nmc_parser *parser, struct footnote *footnote)
{
        bool found = false;
        struct anchor *p = NULL;
        list_for_each_safe(struct anchor, c, n, parser->anchors) {
                if (id_eq(&c->id, &footnote->id)) {
                        if (footnote->node != NULL) {
                                c->node->node.node.type = footnote->node->node.node.type;
                                c->node->node.node.name = footnote->node->node.node.name;
                                c->node->u.auxiliary.name = footnote->node->name;
                                c->node->u.auxiliary.attributes = footnote->node->attributes;
                                c->node = NULL;
                        } else
                                c->node->u.anchor = NULL;
                        if (p == NULL)
                                parser->anchors = n;
                        else
                                p->next = n;
                        anchor_free1(c);
                        found = true;
                } else
                        p = c;
        }
        if (found)
                footnote->node = NULL;
        else
                nmc_parser_error(parser,
                                 &footnote->location,
                                 "unreferenced footnote: %s", footnote->id.string);
}

static void
footnote(struct nmc_parser *parser, struct footnote *footnotes)
{
        list_for_each_safe(struct footnote, p, n, footnotes) {
                update_anchors(parser, p);
                footnote_free(p);
        }
}
#define footnote(parser, result, footnotes) (footnote(parser, footnotes), result)

static inline struct footnote *
fibling(struct nmc_parser *parser, struct footnote *footnotes, struct footnote *footnote)
{
        struct footnote *last;
        list_for_each(struct footnote, p, footnotes) {
                if (id_eq(&footnote->id, &p->id)) {
                        char *s = nmc_location_str(&p->location);
                        nmc_parser_error(parser, &footnote->location,
                                         "footnote already defined at %s: %s", s, p->id.string);
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
                anchor->next = parser->anchors;
                parser->anchors = anchor;
                anchor->location = p->location;
                anchor->id = id_new(strdup(p->id));
                anchor->node = node_new(struct anchor_node, PRIVATE, NODE_ANCHOR);
                anchor->node->u.anchor = anchor;
                if (outermost == atom) {
                        anchor->node->node.children = atom;
                        outermost = (struct node *)anchor->node;
                } else {
                        anchor->node->node.children = ((struct parent_node *)outermost)->children;
                        ((struct parent_node *)outermost)->children = (struct node *)anchor->node;
                }

                sigil_free1(p);
        }
        return outermost;
}

static struct node *
buffer(struct substring substring)
{
        struct buffer_node *n = node_new(struct buffer_node, PRIVATE, NODE_BUFFER);
        n->u.buffer = buffer_new(substring.string, substring.length);
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

        buffer_append(((struct buffer_node *)inlines.last)->u.buffer, substring.string, substring.length);
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
                buffer->u.text = buffer_str_free(buffer->u.buffer);
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

footnote: FOOTNOTE { $$ = footnote_new(parser, &@$, $1.id, $1.buffer); };

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
