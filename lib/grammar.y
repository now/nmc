%code requires
{
#define YYLTYPE struct nmc_location
struct nmc_parser;

#include <stdbool.h>
#include <stddef.h>
#include <nmc.h>

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

struct node *anchor_node_new(YYLTYPE *location, const char *string, size_t length);

struct node *text_node_new(enum node_name name, char *text);
struct node *text_node_new_dup(enum node_name name, const char *string, size_t length);

struct footnote *footnote_new(YYLTYPE *location, char *id, const char *content,
                              struct nmc_error **error);
}

%code
{
#include <config.h>

#include <sys/types.h>
#include <regex.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmc/list.h>

#include <private.h>

#include "buffer.h"
#include "ext.h"
#include "parser.h"

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
        free(anchor);
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
                struct anchor *anchor;
                struct {
                        const char *name;
                        struct auxiliary_node_attributes *attributes;
                } auxiliary;
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

#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static struct nmc_error *
nmc_regerror(int errcode, const regex_t *regex, const char *message)
{
        size_t l = regerror(errcode, regex, NULL, 0);
        char *s = malloc(l);
        if (s == NULL)
                return &nmc_oom_error;
        regerror(errcode, regex, s, l);
        struct nmc_error *e = nmc_error_newu(message, s);
        free(s);
        return e == NULL ? &nmc_oom_error : e;
}
#pragma GCC diagnostic warning "-Wformat-nonliteral"

static bool
definitions_push(const char *pattern, definefn define, struct nmc_error **error)
{
        struct definition *definition = malloc(sizeof(struct definition));
        if (definition == NULL) {
                *error = &nmc_oom_error;
                return false;
        }
        int r = regcomp(&definition->regex, pattern, REG_EXTENDED);
        if (r != 0) {
                *error = nmc_regerror(r, &definition->regex,
                                      "definition regex compilation failed: %s");
                free(definition);
                return false;
        }
        definition->define = define;
        definitions = list_cons(definition, definitions);
        return true;
}

static inline struct node *
node_init(struct node *node, enum node_type type, enum node_name name)
{
        if (node == NULL)
                return NULL;
        node->next = NULL;
        node->type = type;
        node->name = name;
        return node;
}
#define node_new(stype, type, name) ((stype *)node_init(malloc(sizeof(stype)), type, name))

static struct auxiliary_node *
auxiliary_node_new_matches(const char *name, const char *buffer,
                           regmatch_t *matches, int n, ...)
{
        struct auxiliary_node *d = node_new(struct auxiliary_node, AUXILIARY, NODE_AUXILIARY);
        d->node.children = NULL;
        d->name = name;
        d->attributes = malloc(sizeof(struct auxiliary_node_attributes) +
                               sizeof(struct auxiliary_node_attribute) * (n + 1));
        d->attributes->references = 1;
        regmatch_t *m = &matches[1];
        struct auxiliary_node_attribute *a = d->attributes->items;
        va_list args;
        va_start(args, n);
        for (int i = 0; i < n; i++) {
                /* TODO Check that rm_so/rm_eo ≠ -1 */
                a->name = va_arg(args, const char *);
                a->value = strndup(buffer + m->rm_so, m->rm_eo - m->rm_so);
                m++;
                a++;
        }
        va_end(args);
        a->name = NULL;
        a->value = NULL;
        return d;
}

static struct auxiliary_node *
abbreviation(const char *buffer, regmatch_t *matches)
{
        return auxiliary_node_new_matches("abbreviation", buffer, matches, 1, "for");
}

static struct auxiliary_node *
ref(const char *buffer, regmatch_t *matches)
{
        return auxiliary_node_new_matches("ref", buffer, matches, 2, "title", "uri");
}

bool
nmc_grammar_initialize(struct nmc_error **error)
{
        if (definitions != NULL)
                return true;
        return  definitions_push("^(.+) +at +(.+)", ref, error) &&
                definitions_push("^Abbreviation +for +(.+)", abbreviation, error);
}

void
nmc_grammar_finalize(void)
{
        list_for_each_safe(struct definition, p, n, definitions) {
                regfree(&p->regex);
                free(p);
        }
        definitions = NULL;
}

static struct auxiliary_node *
define(const char *content, struct nmc_error **error)
{
        list_for_each(struct definition, p, definitions) {
                regmatch_t matches[p->regex.re_nsub + 1];
                int r = regexec(&p->regex, content,
                                p->regex.re_nsub + 1, matches, 0);
                if (r == 0)
                        return p->define(content, matches);
                else if (r != REG_NOMATCH) {
                        *error = nmc_regerror(r, &p->regex,
                                              "definition regex execution failed: %s");
                        return NULL;
                }
        }

        *error = nmc_error_newu("unrecognized footnote content: %s", content);
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
        if (--node->attributes->references == 0) {
                for (struct auxiliary_node_attribute *a = node->attributes->items; a->name != NULL; a++)
                        free(a->value);
                free(node->attributes);
        }
        return parent_node_free((struct parent_node *)node);
}

struct node *
text_node_new(enum node_name name, char *text)
{
        if (text == NULL)
                return NULL;
        struct text_node *n = node_new(struct text_node, TEXT, name);
        if (n == NULL) {
                free(text);
                return NULL;
        }
        n->text = text;
        return (struct node *)n;
}

struct node *
text_node_new_dup(enum node_name name, const char *string, size_t length)
{
        return text_node_new(name, strndup(string, length));
}

static struct node *
text_node_free(struct text_node *node)
{
        free(node->text);
        return NULL;
}

struct node *
anchor_node_new(YYLTYPE *location, const char *string, size_t length)
{
        struct anchor_node *n = node_new(struct anchor_node, PRIVATE, NODE_ANCHOR);
        if (n == NULL)
                return NULL;
        n->u.anchor = malloc(sizeof(struct anchor));
        if (n->u.anchor == NULL) {
                free(n);
                return NULL;
        }
        n->u.anchor->next = NULL;
        n->u.anchor->location = *location;
        char *id = malloc(length + 1);
        if (id == NULL) {
                free(n->u.anchor);
                free(n);
                return NULL;
        }
        memcpy(id, string, length);
        id[length] = '\0';
        n->u.anchor->id = id_new(id);
        n->u.anchor->node = n;
        return (struct node *)n;
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
                free(p);
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

struct footnote *
footnote_new(YYLTYPE *location, char *id, const char *content, struct nmc_error **error)
{
        struct footnote *footnote = malloc(sizeof(struct footnote));
        if (footnote == NULL) {
                free(id);
                return NULL;
        }
        footnote->next = NULL;
        footnote->location = *location;
        footnote->id = id_new(id);
        footnote->node = define(content, error);
        if (footnote->node == NULL)
                (*error)->location = *location;
        return footnote;
}

static void
footnote_free1(struct footnote *footnote)
{
        free(footnote->id.string);
        node_free((struct node *)footnote->node);
        free(footnote);
}

static void
footnote_free(struct footnote *footnote)
{
        list_for_each_safe(struct footnote, p, n, footnote)
                footnote_free1(p);
}

static void
report_remaining_anchors(struct nmc_parser *parser)
{
        struct nmc_error *first = NULL, *previous = NULL, *last = NULL;
        bool oom = false;
        list_for_each_safe(struct anchor, p, n, parser->anchors) {
                if (!oom) {
                        first = nmc_error_new(&p->location,
                                              "reference to undefined footnote: %s",
                                              p->id.string);
                        if (first == NULL)
                                oom = true;
                        else {
                                if (last == NULL)
                                        last = first;
                                if (previous != NULL)
                                        first->next = previous;
                                previous = first;
                        }
                }
                p->node->u.anchor = NULL;
                anchor_free1(p);
        }
        parser->anchors = NULL;
        if (oom) {
                nmc_error_free(previous);
                first = last = &nmc_oom_error;
        }
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
%token <node> TITLE
%token <substring> WORD
%token PARAGRAPH
%token <substring> SPACE
%token CONTINUATION
%token <substring> BLOCKSEPARATOR
%token ITEMIZATION
%token ENUMERATION
%token <node> TERM
%token QUOTE
%token ATTRIBUTION
%token TABLESEPARATOR
%token ROW
%token ENTRYSEPARATOR "entry separator"
%token <node> CODEBLOCK
%token <footnote> FOOTNOTE
%token SECTION
%token INDENT
%token DEDENT
%token <node> CODE
%token <node> EMPHASIS
%token <node> ANCHOR
%token ANCHORSEPARATOR
%token BEGINGROUP
%token ENDGROUP

%type <node> documenttitle
%type <nodes> oblockssections0 blockssections blocks sections oblockssections
%type <node> block footnotedsection section title
%type <footnote> footnotes
%type <node> paragraph
%type <node> itemization itemizationitem
%type <nodes> itemizationitems
%type <node> enumeration enumerationitem
%type <nodes> enumerationitems
%type <node> definitions definition
%type <nodes> definitionitems
%type <node> quote line attribution
%type <nodes> lines
%type <node> table head body row entry
%type <nodes> headbody rows entries
%type <nodes> inlines sinlines
%type <node> oanchoredinline anchoredinline inline
%type <node> item
%type <nodes> oblocks

%union {
        struct substring substring;
        struct nodes nodes;
        struct node *node;
        struct footnote *footnote;
}

%printer { fprintf(yyoutput, "%.*s", (int)$$.length, $$.string); } <substring>
%printer { fprintf(yyoutput, "%d, %d", $$.first->name, $$.last->name); } <nodes>
%printer { fprintf(yyoutput, "%d", $$->name); } <node>
%printer {
        if ($$->node != NULL)
                fprintf(yyoutput, "%s %d", $$->id.string, $$->node->node.node.name);
        else
                fprintf(yyoutput, "%s (unrecognized)", $$->id.string);
} <footnote>

%destructor { node_unlink_and_free(parser, $$.first); } <nodes>
%destructor { node_unlink_and_free(parser, $$); } <node>
%destructor { footnote_free($$); } <footnote>

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

static inline struct nodes
nodes(struct node *node)
{
        return (struct nodes){ node, node };
}

static inline struct nodes
sibling(struct nodes siblings, struct node *sibling)
{
        if (siblings.last == NULL)
                return siblings;
        siblings.last->next = sibling;
        return (struct nodes){ siblings.first, sibling };
}

static inline struct nodes
siblings(struct nodes siblings, struct nodes rest)
{
        siblings.last->next = rest.first;
        return (struct nodes){ siblings.first, rest.last };
}

static inline struct node *
parent1(enum node_name name, struct node *children)
{
        if (children == NULL)
                return NULL;
        struct parent_node *n = node_new(struct parent_node, PARENT, name);
        if (n == NULL)
                return NULL;
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
        if (first == NULL)
                return NULL;
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
                                footnote->node->attributes->references++;
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
                footnote_free1(p);
        }
}
#define footnote(parser, result, footnotes) (footnote(parser, footnotes), result)

static inline struct footnote *
fibling(struct nmc_parser *parser, struct footnote *footnotes, struct footnote *footnote)
{
        if (footnote == NULL)
                return NULL;
        struct footnote *last;
        list_for_each(struct footnote, p, footnotes) {
                if (id_eq(&footnote->id, &p->id)) {
                        char *s = nmc_location_str(&p->location);
                        if (s == NULL)
                                return NULL;
                        if (!nmc_parser_error(parser, &footnote->location,
                                              "footnote %s already defined at %s",
                                              p->id.string, s)) {
                                free(s);
                                return NULL;
                        }
                        free(s);
                        footnote_free1(footnote);
                        return footnotes;
                }
                last = p;
        }
        last->next = footnote;
        return footnotes;
}

static struct node *
definition(struct node *term, struct node *item)
{
        if (term == NULL)
                return NULL;
        term->next = parent1(NODE_DEFINITION, ((struct parent_node *)item)->children);
        if (term->next == NULL)
                return NULL;
        ((struct parent_node *)item)->children = term;
        return item;
}

static inline struct node *
anchor(struct nmc_parser *parser, struct node *atom, struct node *anchor)
{
        if (anchor == NULL)
                return NULL;
        ((struct parent_node *)anchor)->children = atom;
        ((struct anchor_node *)anchor)->u.anchor->next = parser->anchors;
        parser->anchors = ((struct anchor_node *)anchor)->u.anchor;
        return anchor;
}

static inline struct node *
wanchor(struct nmc_parser *parser, struct substring substring, struct node *a)
{
        if (a == NULL)
                return NULL;
        struct node *n = text_node_new_dup(NODE_TEXT, substring.string, substring.length);
        if (n == NULL)
                return NULL;
        struct node *r = anchor(parser, n, a);
        if (r == NULL) {
                node_free(n);
                return NULL;
        }
        return r;
}

static struct node *
buffer(struct substring substring)
{
        struct buffer_node *n = node_new(struct buffer_node, PRIVATE, NODE_BUFFER);
        if (n == NULL)
                return NULL;
        n->u.buffer = buffer_new(substring.string, substring.length);
        if (n->u.buffer == NULL) {
                free(n);
                return NULL;
        }
        return (struct node *)n;
}

static inline struct nodes
append_text(struct nodes inlines, struct substring substring)
{
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name != NODE_BUFFER)
                return sibling(inlines, buffer(substring));

        if (!buffer_append(((struct buffer_node *)inlines.last)->u.buffer,
                           substring.string, substring.length))
                return nodes(NULL);
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
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name == NODE_BUFFER) {
                struct buffer_node *buffer = (struct buffer_node *)inlines.last;
                buffer->node.type = TEXT;
                buffer->node.name = NODE_TEXT;
                struct buffer *b = buffer->u.buffer;
                buffer->u.text = buffer_str(b);
                // TODO Hack until we reuse the buffer
                b->content = NULL;
                buffer_free(b);
        }
        return inlines;
}

#define M(n) do { if ((n) == NULL) { nmc_parser_oom(parser); YYABORT; } } while (0)
#define N(n) M((n).last)
}

%%

nmc: documenttitle oblockssections0 {
        M(parser->doc = parent_children(NODE_DOCUMENT, $1, $2));
        report_remaining_anchors(parser);
};

documenttitle: TITLE { M($$ = parent1(NODE_TITLE, $1)); };

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
| CODEBLOCK
| table;

sections: footnotedsection { $$ = nodes($1); }
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes oblockseparator { $$ = footnote(parser, $1, $2); };

oblockseparator: /* empty */
| BLOCKSEPARATOR;

section: SECTION { parser->want = INDENT; } title oblockssections { M($$ = parent_children(NODE_SECTION, $3, $4)); };

title: inlines { M($$ = parent(NODE_TITLE, $1)); };

oblockssections: /* empty */ { $$ = nodes(NULL); }
| INDENT blockssections DEDENT { $$ = $2; };

footnotes: FOOTNOTE { M($$ = $1); }
| footnotes FOOTNOTE { M($$ = fibling(parser, $1, $2)); };

paragraph: PARAGRAPH inlines { M($$ = parent(NODE_PARAGRAPH, $2)); };

itemization: itemizationitems { M($$ = parent(NODE_ITEMIZATION, $1)); };

itemizationitems: itemizationitem { $$ = nodes($1); }
| itemizationitems itemizationitem { $$ = sibling($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

enumeration: enumerationitems { M($$ = parent(NODE_ENUMERATION, $1)); };

enumerationitems: enumerationitem { $$ = nodes($1); }
| enumerationitems enumerationitem { $$ = sibling($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

definitions: definitionitems { M($$ = parent(NODE_DEFINITIONS, $1)); };

definitionitems: definition { $$ = nodes($1); }
| definitionitems definition { $$ = sibling($1, $2); };

definition: TERM item { M($$ = definition($1, $2)); };

quote: lines attribution { M($$ = parent(NODE_QUOTE, sibling($1, $2))); };

lines: line { $$ = nodes($1); }
| lines line { $$ = sibling($1, $2); };

line: QUOTE inlines { M($$ = parent(NODE_LINE, $2)); };

attribution: /* empty */ { $$ = NULL; }
| ATTRIBUTION inlines { M($$ = parent(NODE_ATTRIBUTION, $2)); };

table: headbody { M($$ = parent(NODE_TABLE, $1)); }

headbody: head body { $$ = sibling(nodes($1), $2); }
| body { $$ = nodes($1); };

head: row TABLESEPARATOR { M($$ = parent1(NODE_HEAD, $1)); };

body: rows { M($$ = parent(NODE_BODY, $1)); };

rows: row { $$ = nodes($1); }
| rows row { $$ = sibling($1, $2); };

row: ROW entries ENTRYSEPARATOR { M($$ = parent(NODE_ROW, $2)); };

entries: entry { $$ = nodes($1); }
| entries ENTRYSEPARATOR entry { $$ = sibling($1, $3); };

entry: inlines { M($$ = parent(NODE_ENTRY, $1)); };

inlines: ospace sinlines ospace { $$ = textify($2); };

sinlines: WORD { N($$ = nodes(buffer($1))); }
| oanchoredinline { $$ = nodes($1); }
/* TODO $1.last can, if I see it correctly, never be NODE_BUFFER, so append_text may be unnecessary here. */
| sinlines WORD { N($$ = append_text($1, $2)); }
| sinlines oanchoredinline { N($$ = sibling(textify($1), $2)); }
| sinlines spaces WORD { N($$ = append_text(append_space($1), $3)); }
| sinlines spaces oanchoredinline { N($$ = sibling(textify(append_space($1)), $3)); };

oanchoredinline: inline
| anchoredinline;

anchoredinline: inline ANCHOR { M($$ = anchor(parser, $1, $2)); }
| WORD ANCHOR { M($$ = wanchor(parser, $1, $2)); }
| anchoredinline ANCHORSEPARATOR ANCHOR { M($$ = anchor(parser, $1, $3)); }

inline: CODE { M($$ = $1); }
| EMPHASIS { M($$ = $1); }
| BEGINGROUP sinlines ENDGROUP { M($$ = parent(NODE_GROUP, textify($2))); };

ospace: /* empty */
| SPACE;

spaces: spacecontinuation
| spaces spacecontinuation;

spacecontinuation: SPACE
| CONTINUATION;

item: { parser->want = INDENT; } inlines oblocks { M($$ = parent_children(NODE_ITEM, parent(NODE_PARAGRAPH, $2), $3)); };

oblocks: /* empty */ { $$ = nodes(NULL); }
| INDENT blocks DEDENT { $$ = $2; };
