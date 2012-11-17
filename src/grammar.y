%code requires
{
struct nmc_parser;

void nmc_grammar_initialize(void);
void nmc_grammar_finalize(void);

struct nodes
{
        struct node *first;
        struct node *last;
};
}

%code top
{
#include <libxml/tree.h>
}
%code
{
#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdbool.h>

#include "list.h"
#include "nmc.h"
#include "node.h"
#include "parser.h"

struct anchor
{
        YYLTYPE location;
        struct auxiliary_node *node;
        struct node *child;
        xmlChar *id;
};

static void
anchor_unlink(struct node *node, struct nmc_parser *parser)
{
        if (node->type != NODE_ANCHOR)
                return;
        xmlListPtr anchors = xmlHashLookup(parser->anchors, node->u.anchor->id);
        if (anchors != NULL)
                xmlListRemoveFirst(anchors, node->u.anchor);
}

static void
node_unlink_and_free(struct nmc_parser *parser, struct node *node)
{
        nmc_node_traverse(node, (traversefn)anchor_unlink, nmc_node_traverse_null, parser);
        /* TODO And what if node is a NODE_ANCHOR?  This will be easier to
         * manage once we don’t have anchors and footnotes in xmlLists. */
        /* TODO What if nmc_node_traverse fails (due to OOM)?  We still need to
         * free anchors in node_free and we need to clear out
         * parser->anchors. */
        node_free(node);
}

typedef struct auxiliary_node *(*definefn)(const xmlChar *, regmatch_t *);

struct definition
{
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

static struct auxiliary_node *
definition_element(const char *name, const xmlChar *buffer, regmatch_t *matches, const char **attributes)
{
        int n = 0;
        for (const char **p = attributes; *p != NULL; p++)
                n++;
        struct auxiliary_node *d = nmc_new(struct auxiliary_node);
        d->node.next = NULL;
        d->node.type = NODE_AUXILIARY;
        d->node.u.children = NULL;
        d->name = name;
        d->attributes = nmc_new_n(struct auxiliary_node_attribute, n + 1);
        regmatch_t *m = &matches[1];
        struct auxiliary_node_attribute *a = d->attributes;
        for (const char **p = attributes; *p != NULL; p++) {
                /* TODO Check that rm_so/rm_eo ≠ -1 */
                a->name = *p;
                a->value = (char *)xmlStrndup(buffer + m->rm_so, m->rm_eo - m->rm_so);
                m++;
                a++;
        }
        a->name = NULL;
        a->value = NULL;
        return d;
}

static struct auxiliary_node *
abbreviation(const xmlChar *buffer, regmatch_t *matches)
{
        return definition_element("abbreviation", buffer, matches,
                                  (const char *[]){ "for", NULL });
}

static struct auxiliary_node *
ref(const xmlChar *buffer, regmatch_t *matches)
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
define(const xmlChar *content)
{
        list_for_each(struct definition, p, definitions) {
                regmatch_t matches[p->regex.re_nsub + 1];

                if (regexec(&p->regex, (const char *)content,
                            p->regex.re_nsub + 1, matches, 0) == 0)
                        return p->define(content, matches);
        }

        return NULL;
}

struct footnote
{
        YYLTYPE location;
        xmlChar *id;
        struct auxiliary_node *node;
        bool referenced;
};

static struct footnote *
footnote_new(YYLTYPE *location, xmlChar *id, xmlBufferPtr buffer)
{
        struct footnote *footnote = nmc_new(struct footnote);
        footnote->location = *location;
        footnote->id = id;
        footnote->node = define(xmlBufferContent(buffer));
        footnote->referenced = false;
        return footnote;
}

static void
footnote_free1(struct footnote *footnote)
{
        xmlFree(footnote->id);
        node_free((struct node *)footnote->node);
        nmc_free(footnote);
}

static void
footnote_free(xmlLinkPtr link)
{
        footnote_free1((struct footnote *)xmlLinkGetData(link));
}

struct sigil
{
        YYLTYPE location;
        xmlChar *id;
};

static struct sigil *
sigil_new(YYLTYPE *location, const xmlChar *string, int length)
{
        struct sigil *sigil = nmc_new(struct sigil);
        sigil->location = *location;
        sigil->id = xmlCharStrndup((const char *)string, length);
        return sigil;
}

static void
sigil_free(xmlLinkPtr link)
{
        struct sigil *sigil = (struct sigil *)xmlLinkGetData(link);
        xmlFree(sigil->id);
        nmc_free(sigil);
}

static void
anchor_free(xmlLinkPtr link)
{
        struct anchor *anchor = (struct anchor *)xmlLinkGetData(link);
        /* Do we ever have to free anchor->node?  I mean, it’s always on the stack or in the tree, right? */
        // node_free((struct node *)anchor->node);
        /* TODO And what if anchor->child is a NODE_ANCHOR?  This will be
         * easier to manage once we don’t have anchors and footnotes in
         * xmlLists. */
        node_free(anchor->child);
        xmlFree(anchor->id);
        nmc_free(anchor);
}

/* TODO Move this to parser.c? */
static int
report_remaining_anchor(struct anchor *anchor, struct nmc_parser *parser)
{
        nmc_parser_error(parser, &anchor->location,
                         "reference to undefined footnote: %s",
                         (const char *)anchor->id);
        return 1;
}

static void
report_remaining_anchors(xmlListPtr list, struct nmc_parser *parser, UNUSED(const xmlChar *name))
{
        xmlListWalk(list, (xmlListWalker)report_remaining_anchor, parser);
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
%type <list> footnotes
%type <footnote> footnote
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
%type <list> sigils
%type <sigil> sigil
%type <node> item
%type <nodes> oblocks

%union {
        struct {
                const xmlChar *string;
                int length;
        } substring;
        struct {
                xmlChar *id;
                xmlBufferPtr buffer;
        } raw_footnote;
        xmlBufferPtr buffer;
        struct node *node;
        struct nodes nodes;
        xmlListPtr list;
        struct footnote *footnote;
        struct sigil *sigil;
}

%printer { fprintf(yyoutput, "%.*s", $$.length, $$.string); } <substring>
%printer { fprintf(yyoutput, "%s %s", $$.id, xmlBufferContent($$.buffer)); } <raw_footnote>
%printer { fprintf(yyoutput, "%s", xmlBufferContent($$)); } <buffer>

%destructor { xmlFree($$.id); xmlBufferFree($$.buffer); } <raw_footnote>
%destructor { xmlBufferFree($$); } <buffer>
%destructor { node_unlink_and_free(parser, $$.first); } <nodes>
%destructor { node_unlink_and_free(parser, $$); } <node>
%destructor { xmlListDelete($$); } <list>
%destructor { footnote_free1($$); } <footnote>
/* TODO This is almost not necessary, but still…
%destructor { sigil_free($$); } <sigil>
*/

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
nmc_grammar_error(YYLTYPE *location,
                  struct nmc_parser *parser,
                  const char *message)
{
        nmc_parser_error(parser, location,
                         "%s", message);
}

static struct node *
node(enum node_type type)
{
        struct node *n = nmc_new(struct node);
        n->next = NULL;
        n->type = type;
        return n;
}

static char *
detach(xmlBufferPtr buffer)
{
        int l = xmlBufferLength(buffer);
        char *r = (char *)xmlBufferDetach(buffer);
        char *p = (char *)xmlRealloc(r, l);
        xmlBufferFree(buffer);
        return p != NULL ? p : r;
}

static struct node *
text(enum node_type type, xmlBufferPtr buffer)
{
        struct node *n = node(type);
        n->u.children = node(NODE_TEXT);
        n->u.children->u.text = detach(buffer);
        return n;
}

static struct node *
content(enum node_type type, xmlBufferPtr buffer)
{
        struct node *n = node(type);
        n->u.text = detach(buffer);
        return n;
}

static struct node *
scontent(enum node_type type, const xmlChar *string, int length)
{
        struct node *n = node(type);
        n->u.text = (char *)xmlStrndup(string, length);
        return n;
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
wrap1(enum node_type type, struct node *children)
{
        struct node *n = node(type);
        n->u.children = children;
        return n;
}

static inline struct node *
wrap(enum node_type type, struct nodes children)
{
        return wrap1(type, children.first);
}

static inline struct node *
wrap_children(enum node_type type, struct node *first, struct nodes rest)
{
        first->next = rest.first;
        return wrap1(type, first);
}

static xmlListPtr
push(xmlListPtr list, void *item)
{
        xmlListPushBack(list, item);
        return list;
}

static xmlListPtr
list(xmlListDeallocator deallocator, void *item)
{
        xmlListPtr list = xmlListCreate(deallocator, NULL);
        return push(list, item);
}

static int
update_anchor(struct anchor *anchor, struct auxiliary_node *node)
{
        anchor->node->node.type = node->node.type;
        anchor->node->node.u.children = anchor->child;
        anchor->node->name = node->name;
        anchor->node->attributes = node->attributes;
        anchor->node = NULL;
        anchor->child = NULL;
        return 1;
}

static int
footnote_reference(struct footnote *footnote, struct nmc_parser *parser)
{
        xmlListPtr anchors = xmlHashLookup(parser->anchors, footnote->id);
        if (anchors == NULL)
                return 1;

        if (footnote->node != NULL)
                xmlListWalk(anchors, (xmlListWalker)update_anchor, footnote->node);
        footnote->node = NULL;
        xmlHashRemoveEntry(parser->anchors,
                           footnote->id,
                           (xmlHashDeallocator)xmlListDelete);
        footnote->referenced = true;

        return 1;
}

static int
footnote_check(struct footnote *footnote, struct nmc_parser *parser)
{
        if (!footnote->referenced)
                nmc_parser_error(parser,
                                 &footnote->location,
                                 "unreferenced footnote: %s", footnote->id);

        return 1;
}

static void
footnote(struct nmc_parser *parser, xmlListPtr footnotes)
{
        xmlListWalk(footnotes, (xmlListWalker)footnote_reference, parser);
        xmlListWalk(footnotes, (xmlListWalker)footnote_check, parser);
        xmlListDelete(footnotes);
}
#define footnote(parser, result, footnotes) (footnote(parser, footnotes), result)

static struct node *
definition(const xmlChar *string, int length, struct node *item)
{
        struct node *n = scontent(NODE_TERM, string, length);
        n->next = node(NODE_DEFINITION);
        n->next->u.children = item->u.children;
        item->u.children = n;

        return item;
}

struct anchor_closure
{
        struct nmc_parser *parser;
        struct node *node;
};

static int
anchor_1(struct sigil *sigil, struct anchor_closure *closure)
{
        struct anchor *anchor = nmc_new(struct anchor);
        anchor->location = sigil->location;
        anchor->id = xmlStrdup(sigil->id);
        anchor->child = closure->node;
        anchor->node = nmc_new(struct auxiliary_node);
        anchor->node->node.next = NULL;
        anchor->node->node.type = NODE_ANCHOR;
        anchor->node->node.u.anchor = anchor;
        closure->node = (struct node *)anchor->node;

        xmlListPtr anchors = xmlHashLookup(closure->parser->anchors, anchor->id);
        if (anchors == NULL) {
                anchors = xmlListCreate(anchor_free, NULL);
                xmlHashAddEntry(closure->parser->anchors, anchor->id, anchors);
        }
        xmlListPushBack(anchors, anchor);

        return 1;
}

static struct node *
anchor(struct nmc_parser *parser, struct node *atom, xmlListPtr sigils)
{
        if (sigils == NULL)
                return atom;

        struct anchor_closure closure = { parser, atom };
        xmlListWalk(sigils, (xmlListWalker)anchor_1, &closure);
        xmlListDelete(sigils);
        return closure.node;
}

static struct node *
buffer(const xmlChar *string, int length)
{
        struct node *n = node(NODE_BUFFER);
        n->u.buffer = xmlBufferCreateSize(length);
xmlBufferSetAllocationScheme(n->u.buffer,XML_BUFFER_ALLOC_EXACT);
        xmlBufferAdd(n->u.buffer, string, length);
        return n;
}

static struct node *
word(struct nmc_parser *parser, const xmlChar *string, int length, xmlListPtr sigils)
{
        if (sigils == NULL)
                return buffer(string, length);
        return anchor(parser, scontent(NODE_TEXT, string, length), sigils);
}

static inline struct nodes
append_text(struct nodes inlines, const xmlChar *string, int length)
{
        if (inlines.last->type == NODE_BUFFER) {
                xmlBufferAdd(inlines.last->u.buffer, string, length);
                return inlines;
        }

        return sibling(inlines, buffer(string, length));
}

static inline struct nodes
append_space(struct nodes inlines)
{
        return append_text(inlines, BAD_CAST " ", 1);
}

static inline struct nodes
textify(struct nodes inlines)
{
        if (inlines.last->type == NODE_BUFFER) {
                inlines.last->type = NODE_TEXT;
                xmlBufferPtr buffer = inlines.last->u.buffer;
                inlines.last->u.text = detach(buffer);
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
append_word(struct nmc_parser *parser, struct nodes inlines, const xmlChar *string, int length, xmlListPtr sigils)
{
        return (sigils != NULL) ?
                inline_sibling(inlines, word(parser, string, length, sigils)) :
                append_text(inlines, string, length);
}

static inline struct nodes
append_spaced_word(struct nmc_parser *parser, struct nodes inlines, const xmlChar *string, int length, xmlListPtr sigils)
{
        return (sigils != NULL) ?
                append_inline(inlines, word(parser, string, length, sigils)) :
                append_text(append_space(inlines), string, length);
}
}

%%

nmc: TITLE oblockssections0 {
        parser->doc = wrap_children(NODE_DOCUMENT, text(NODE_TITLE, $1), $2);
        xmlHashScan(parser->anchors, (xmlHashScanner)report_remaining_anchors, parser);
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
| CODEBLOCK { $$ = content(NODE_CODEBLOCK, $1); }
| table;

sections: footnotedsection { $$ = nodes($1); }
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes oblockseparator { $$ = footnote(parser, $1, $2); };

oblockseparator: /* empty */
| BLOCKSEPARATOR;

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = wrap_children(NODE_SECTION, $3, $4); };

title: inlines { $$ = wrap(NODE_TITLE, $1); };

oblockssections: /* empty */ { $$ = nodes(NULL); }
| INDENT blockssections DEDENT { $$ = $2; };

footnotes: footnote { $$ = list(footnote_free, $1); }
| footnotes footnote { $$ = push($1, $2); };

footnote: FOOTNOTE {
        $$ = footnote_new(&@$, $1.id, $1.buffer);
        if ($$->node == NULL)
                nmc_parser_error(parser, &@$,
                                 "unrecognized footnote content: %s",
                                 xmlBufferContent($1.buffer));
        xmlBufferFree($1.buffer);
};

paragraph: PARAGRAPH inlines { $$ = wrap(NODE_PARAGRAPH, $2); };

itemization: itemizationitems { $$ = wrap(NODE_ITEMIZATION, $1); };

itemizationitems: itemizationitem { $$ = nodes($1); }
| itemizationitems itemizationitem { $$ = sibling($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

enumeration: enumerationitems { $$ = wrap(NODE_ENUMERATION, $1); };

enumerationitems: enumerationitem { $$ = nodes($1); }
| enumerationitems enumerationitem { $$ = sibling($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

definitions: definitionitems { $$ = wrap(NODE_DEFINITIONS, $1); };

definitionitems: definition { $$ = nodes($1); }
| definitionitems definition { $$ = sibling($1, $2); };

definition: DEFINITION item { $$ = definition($1.string, $1.length, $2); };

quote: lines attribution { $$ = wrap(NODE_QUOTE, sibling($1, $2)); };

lines: line { $$ = nodes($1); }
| lines line { $$ = sibling($1, $2); };

line: QUOTE inlines { $$ = wrap(NODE_LINE, $2); };

attribution: /* empty */ { $$ = NULL; }
| ATTRIBUTION inlines { $$ = wrap(NODE_ATTRIBUTION, $2); };

table: headbody { $$ = wrap(NODE_TABLE, $1); }

headbody: row { $$ = nodes(wrap1(NODE_BODY, $1)); }
| row TABLESEPARATOR body { $$ = sibling(nodes(wrap1(NODE_HEAD, $1)), wrap(NODE_BODY, $3)); }
| row body { $$ = nodes(wrap(NODE_BODY, sibling(nodes($1), $2.first))); };

body: row { $$ = nodes($1); }
| body row { $$ = sibling($1, $2); };

row: ROW entries ENTRYSEPARATOR { $$ = wrap(NODE_ROW, $2); };

entries: entry { $$ = nodes($1); }
| entries ENTRYSEPARATOR entry { $$ = sibling($1, $3); };

entry: inlines { $$ = wrap(NODE_ENTRY, $1); };

inlines: ospace sinlines ospace { $$ = textify($2); };

sinlines: WORD sigils { $$ = nodes(word(parser, $1.string, $1.length, $2)); }
| anchoredinline { $$ = nodes($1); }
| sinlines WORD sigils { $$ = append_word(parser, $1, $2.string, $2.length, $3); }
| sinlines anchoredinline { $$ = sibling($1, $2); }
| sinlines spaces WORD sigils { $$ = append_spaced_word(parser, $1, $3.string, $3.length, $4); }
| sinlines spaces anchoredinline { $$ = append_inline($1, $3); };

anchoredinline: inline sigils { $$ = anchor(parser, $1, $2); };

inline: CODE { $$ = scontent(NODE_CODE, $1.string, $1.length); }
| EMPHASIS { $$ = scontent(NODE_EMPHASIS, $1.string, $1.length); }
| BEGINGROUP sinlines ENDGROUP { $$ = wrap(NODE_GROUP, textify($2)); };

sigils: /* empty */ { $$ = NULL; }
| sigil { $$ = list(sigil_free, $1); }
| sigils SIGILSEPARATOR sigil { $$ = push($1, $3); };

sigil: SIGIL { $$ = sigil_new(&@$, $1.string, $1.length); };

ospace: /* empty */
| SPACE;

spaces: spacecontinuation
| spaces spacecontinuation;

spacecontinuation: SPACE
| CONTINUATION;

item: { parser->want = INDENT; } inlines oblocks { $$ = wrap_children(NODE_ITEM, wrap(NODE_PARAGRAPH, $2), $3); };

oblocks: /* empty */ { $$ = nodes(NULL); }
| INDENT blocks DEDENT { $$ = $2; };
