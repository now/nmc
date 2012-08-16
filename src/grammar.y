%code top
{
#include <stdio.h>
#include <stdbool.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"

struct footnote
{
        YYLTYPE location;
        xmlChar *id;
        xmlBufferPtr buffer;
        bool referenced;
};

static struct footnote *
footnote_new(YYLTYPE *location, const xmlChar *string, int length, xmlBufferPtr buffer)
{
        struct footnote *footnote =
                (struct footnote *)xmlMalloc(sizeof(struct footnote));
        footnote->location = *location;
        footnote->id = xmlCharStrndup((const char *)string, length);
        footnote->buffer = buffer;
        footnote->referenced = false;
        return footnote;
}

static void
footnote_free(xmlLinkPtr link)
{
        struct footnote *footnote = (struct footnote *)xmlLinkGetData(link);
        xmlFree(footnote->id);
        xmlBufferFree(footnote->buffer);
        xmlFree(footnote);
}

struct sigil
{
        YYLTYPE location;
        xmlChar *id;
};

static struct sigil *
sigil_new(YYLTYPE *location, const xmlChar *string, int length)
{
        struct sigil *sigil =
                (struct sigil *)xmlMalloc(sizeof(struct sigil));
        sigil->location = *location;
        sigil->id = xmlCharStrndup((const char *)string, length);
        return sigil;
}

static void
sigil_free(xmlLinkPtr link)
{
        struct sigil *sigil = (struct sigil *)xmlLinkGetData(link);
        xmlFree(sigil->id);
        xmlFree(sigil);
}

struct anchor
{
        YYLTYPE location;
        xmlNodePtr node;
};

static struct anchor *
anchor_new(YYLTYPE *location, xmlNodePtr node)
{
        struct anchor *anchor =
                (struct anchor *)xmlMalloc(sizeof(struct anchor));
        anchor->location = *location;
        anchor->node = node;
        return anchor;
}

static inline const xmlChar *
anchor_id(struct anchor *anchor)
{
        return xmlHasProp(anchor->node, BAD_CAST "id")->children->content;
}

static void
anchor_free(xmlLinkPtr link)
{
        xmlFree(xmlLinkGetData(link));
}

static int
report_remaining_anchor(struct anchor *anchor, struct nmc_parser *parser)
{
        nmc_parser_error(parser, &anchor->location,
                         "reference to undefined footnote: %s",
                         (const char *)anchor_id(anchor));
        return 1;
}

static void
report_remaining_anchors(xmlListPtr list, struct nmc_parser *parser, UNUSED(const xmlChar *name))
{
        xmlListWalk(list, (xmlListWalker)report_remaining_anchor, parser);
}

static void
clear_anchors(UNUSED(xmlListPtr list), xmlHashTablePtr anchors, const xmlChar *name)
{
        xmlHashRemoveEntry(anchors, name, (xmlHashDeallocator)xmlListDelete);
}

struct find_anchor_closure
{
        xmlNodePtr node;
        struct anchor *anchor;
};

static int
find_anchor(struct anchor *anchor, struct find_anchor_closure *closure)
{
        if (anchor->node != closure->node)
                return 1;
        closure->anchor = anchor;
        return 0;
}

static bool
node_free_anchors_in(struct nmc_parser *parser, xmlXPathContextPtr context)
{
        xmlXPathObjectPtr object = xmlXPathEvalExpression((const xmlChar *)"//anchor", context);
        if (object == NULL)
                return false;

        for (int i = 0; i < object->nodesetval->nodeNr; i++) {
                xmlNodePtr node = object->nodesetval->nodeTab[i];
                const xmlChar *id = xmlHasProp(node, BAD_CAST "id")->children->content;
                xmlListPtr anchors = xmlHashLookup(parser->anchors, id);
                if (anchors != NULL) {
                        struct find_anchor_closure closure = { node, NULL };
                        xmlListWalk(anchors, (xmlListWalker)find_anchor, &closure);
                        if (closure.anchor != NULL)
                                xmlListRemoveFirst(anchors, closure.anchor);
                }
        }

        xmlXPathFreeObject(object);

        return true;
}

static bool
node_free_anchors(struct nmc_parser *parser, xmlNodePtr node)
{
        xmlXPathContextPtr context = xmlXPathNewContext(node->doc);
        if (context == NULL)
                return false;
        context->node = node;
        bool result = node_free_anchors_in(parser, context);
        xmlXPathFreeContext(context);
        return result;
}

static void
node_free(struct nmc_parser *parser, xmlNodePtr node)
{
        if (!node_free_anchors(parser, node))
                xmlHashScan(parser->anchors, (xmlHashScanner)clear_anchors, NULL);
        xmlFreeNode(node);
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
%token ENTRY
%token CODEBLOCK
%token <substring> FOOTNOTE
%token SECTION
%token INDENT
%token DEDENT
%token <substring> CODE
%token <substring> EMPHASIS
%token <substring> SIGIL
%token SIGILSEPARATOR
%token BEGINGROUP
%token ENDGROUP

%type <node> title oblockssections0
%type <node> blockssections
%type <node> blocks block
%type <node> sections footnotedsection section
%type <node> oblockssections
%type <list> footnotes
%type <footnote> footnote
%type <node> paragraph
%type <node> itemization itemizationitem
%type <node> enumeration enumerationitem
%type <node> definitions definition
%type <node> quote line attribution
%type <node> table headbody body row entries entry
%type <node> codeblock
%type <buffer> codeblockwords
%type <buffer> words swords
%type <node> inlines sinlines anchoredinline inline
%type <list> sigils
%type <sigil> sigil
%type <node> item oblocks

%union {
        const xmlChar *string;
        struct {
                const xmlChar *string;
                int length;
        } substring;
        xmlBufferPtr buffer;
        xmlNodePtr node;
        xmlListPtr list;
        struct footnote *footnote;
        struct sigil *sigil;
}

%printer { fprintf(yyoutput, "%.*s", $$.length, $$.string); } <substring>

%destructor { xmlBufferFree($$); } <buffer>
%destructor { node_free(parser, $$); } <node>
%destructor { xmlListDelete($$); } <list>
%destructor { xmlFree($$); } <footnote>

%code
{
static int
nmc_grammar_lex(YYSTYPE *value, YYLTYPE *location, struct nmc_parser *parser)
{
        return nmc_parser_lex(parser, location, value);
}

static void
nmc_grammar_error(YYLTYPE *location,
                  struct nmc_parser *parser,
                  const char *message)
{
        nmc_parser_error(parser, location,
                         "%s: %s", (const char *)parser->p, message);
}

static xmlNodePtr
node(const char *name)
{
        return xmlNewNode(NULL, BAD_CAST name);
}

static xmlNodePtr
content(const char *name, xmlBufferPtr buffer)
{
        xmlNodePtr result = node(name);
        xmlNodeAddContent(result, xmlBufferContent(buffer));
        xmlBufferFree(buffer);
        return result;
}

static xmlNodePtr
scontent(const char *name, const xmlChar *string, int length)
{
        xmlNodePtr result = node(name);
        xmlNodeAddContentLen(result, string, length);
        return result;
}

static xmlNodePtr
child(xmlNodePtr parent, xmlNodePtr child)
{
        if (child == NULL)
                return parent;
        xmlAddChild(parent, child);
        return parent;
}

static xmlNodePtr
children(xmlNodePtr parent, xmlNodePtr children)
{
        if (children == NULL)
                return parent;
        xmlAddChildList(parent, children);
        return parent;
}

static xmlNodePtr
wrap(const char *name, xmlNodePtr kid)
{
        return child(node(name), kid);
}

static xmlNodePtr
sibling(xmlNodePtr first, xmlNodePtr last)
{
        if (first == NULL)
                return last;
        if (last == NULL)
                return first;
        xmlAddSibling(first, last);
        return first;
}

static xmlNodePtr
siblings(xmlNodePtr first, xmlNodePtr rest)
{
        if (first == NULL)
                return rest;
        if (rest == NULL)
                return first;
        xmlNodePtr next = rest->next;
        xmlAddSibling(first, rest);
        rest->next = next;
        return first;
}

static xmlBufferPtr
buffer(const xmlChar *string, int length)
{
        /* TODO May be able to use xmlBufferCreateStatic() */
        xmlBufferPtr buffer = xmlBufferCreate();
        xmlBufferAdd(buffer, string, length);
        return buffer;
}

static xmlBufferPtr
bappend(xmlBufferPtr buffer, const xmlChar *string, int length)
{
        xmlBufferAdd(buffer, string, length);
        return buffer;
}

static xmlBufferPtr
append(xmlBufferPtr buffer, const xmlChar *string, int length)
{
        xmlBufferAdd(buffer, BAD_CAST " ", 1);
        xmlBufferAdd(buffer, string, length);
        return buffer;
}

static xmlNodePtr
codeblock(xmlNodePtr blocks, const xmlChar *string, int length, xmlNodePtr code)
{
        xmlNodePtr last = blocks;
        while (last->next != NULL)
                last = last->next;

        if (!xmlStrEqual(last->name, BAD_CAST "code"))
                return sibling(blocks, code);

        xmlNodeAddContentLen(last, string, length);
        xmlAddChildList(last, code->children);
        code->children = NULL;
        xmlFreeNode(code);
        return blocks;
}

static int
update_anchor(struct anchor *anchor, struct footnote *footnote)
{
        xmlNewProp(anchor->node, BAD_CAST "buffer", xmlBufferContent(footnote->buffer));
        return 1;
}

static int
footnote_reference(struct footnote *footnote, struct nmc_parser *parser)
{
        xmlListPtr anchors = xmlHashLookup(parser->anchors, footnote->id);
        if (anchors != NULL) {
                xmlListWalk(anchors, (xmlListWalker)update_anchor, footnote);
                xmlHashRemoveEntry(parser->anchors,
                                   footnote->id,
                                   (xmlHashDeallocator)xmlListDelete);
                footnote->referenced = true;
        }

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

static xmlNodePtr
footnote(struct nmc_parser *parser, xmlNodePtr blocks, xmlListPtr footnotes)
{
        xmlNodePtr last = blocks;
        while (last->next != NULL)
                last = last->next;

        xmlListWalk(footnotes, (xmlListWalker)footnote_reference, parser);
        xmlListWalk(footnotes, (xmlListWalker)footnote_check, parser);
        xmlListDelete(footnotes);

        return blocks;
}

static xmlNodePtr
definition(const xmlChar *string, int length, xmlNodePtr item)
{
        xmlNodePtr definition = node("definition");
        xmlAddChildList(definition, item->children);
        item->children = NULL;
        xmlAddChild(item, scontent("term", string, length));
        xmlAddChild(item, definition);

        return item;
}

struct anchor_closure
{
        struct nmc_parser *parser;
        xmlNodePtr node;
};

static int
anchor_1(struct sigil *sigil, struct anchor_closure *closure)
{
        struct anchor *anchor = anchor_new(&sigil->location, node("anchor"));
        xmlNewProp(anchor->node, BAD_CAST "id", sigil->id);
        xmlAddChild(anchor->node, closure->node);
        closure->node = anchor->node;

        xmlListPtr anchors = xmlHashLookup(closure->parser->anchors, sigil->id);
        if (anchors == NULL) {
                anchors = xmlListCreate(anchor_free, NULL);
                xmlHashAddEntry(closure->parser->anchors, sigil->id, anchors);
        }
        xmlListPushBack(anchors, anchor);

        return 1;
}

static xmlNodePtr
anchor(struct nmc_parser *parser, xmlNodePtr atom, xmlListPtr sigils)
{
        if (sigils == NULL)
                return atom;

        struct anchor_closure closure = { parser, atom };
        xmlListWalk(sigils, (xmlListWalker)anchor_1, &closure);
        xmlListDelete(sigils);
        return closure.node;
}

static xmlNodePtr
word(struct nmc_parser *parser, const xmlChar *string, int length, xmlListPtr sigils)
{
        return anchor(parser, xmlNewTextLen(string, length), sigils);
}

static xmlNodePtr
append_text(xmlNodePtr inlines, const xmlChar *string, int length)
{
        xmlNodePtr last = inlines;
        while (last->next != NULL)
                last = last->next;

        if (xmlNodeIsText(last)) {
                xmlNodeAddContentLen(last, string, length);
                return last;
        }

        xmlNodePtr text = xmlNewTextLen(string, length);
        sibling(last, text);
        return text;
}

static xmlNodePtr
append_space(xmlNodePtr inlines)
{
        return append_text(inlines, BAD_CAST " ", 1);
}

static xmlNodePtr
append_inline(xmlNodePtr inlines, xmlNodePtr node)
{
        sibling(append_space(inlines), node);
        return inlines;
}

static xmlNodePtr
append_word(struct nmc_parser *parser, xmlNodePtr inlines, const xmlChar *string, int length, xmlListPtr sigils)
{
        if (sigils != NULL)
                return sibling(inlines, word(parser, string, length, sigils));

        append_text(inlines, string, length);
        return inlines;
}

static xmlNodePtr
append_spaced_word(struct nmc_parser *parser, xmlNodePtr inlines, const xmlChar *string, int length, xmlListPtr sigils)
{
        if (sigils != NULL)
                return append_inline(inlines, word(parser, string, length, sigils));

        xmlNodeAddContentLen(append_space(inlines), string, length);
        return inlines;
}
}

%%

nmc: title oblockssections0 {
        xmlDocSetRootElement(parser->doc, children(wrap("nml", $1), $2));
        xmlHashScan(parser->anchors, (xmlHashScanner)report_remaining_anchors, NULL);
}

title: words { $$ = content("title", $1); }

oblockssections0: /* empty */ { $$ = NULL; }
| BLOCKSEPARATOR blockssections { $$ = $2; };

blockssections: blocks
| blocks BLOCKSEPARATOR sections { $$ = siblings($1, $3); }
| sections;

blocks: block
| codeblock
| blocks BLOCKSEPARATOR block { $$ = sibling($1, $3); }
| blocks BLOCKSEPARATOR codeblock { $$ = codeblock($1, $2.string, $2.length, $3); }
| blocks BLOCKSEPARATOR footnotes { $$ = footnote(parser, $1, $3); };

block: paragraph
| itemization
| enumeration
| definitions
| quote
| table;

sections: footnotedsection
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes oblockseparator { $$ = footnote(parser, $1, $2); };

oblockseparator: /* empty */
| BLOCKSEPARATOR;

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = children(wrap("section", $3), $4); };

oblockssections: /* empty */ { $$ = NULL; }
| INDENT blockssections DEDENT { $$ = $2; };

footnotes: footnote { $$ = xmlListCreate(footnote_free, NULL); xmlListPushBack($$, $1); }
| footnotes footnote { $$ = $1; xmlListPushBack($$, $2); };

footnote: FOOTNOTE words { $$ = footnote_new(&@$, $1.string, $1.length, $2); }

paragraph: PARAGRAPH inlines { $$ = wrap("p", $2); };

itemization: itemizationitem { $$ = wrap("itemization", $1); }
| itemization itemizationitem { $$ = child($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

enumeration: enumerationitem { $$ = wrap("enumeration", $1); }
| enumeration enumerationitem { $$ = child($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

definitions: definition { $$ = wrap("definitions", $1); }
| definitions definition { $$ = child($1, $2); };

definition: DEFINITION item { $$ = definition($1.string, $1.length, $2); };

quote: line { $$ = wrap("quote", $1); }
| quote line { $$ = child($1, $2); }
| quote attribution { $$ = child($1, $2); };

line: QUOTE inlines { $$ = wrap("line", $2); };

attribution: ATTRIBUTION inlines { $$ = wrap("attribution", $2); };

table: headbody { $$ = wrap("table", $1); }

headbody: row { $$ = wrap("body", $1); }
| row TABLESEPARATOR body { $$ = sibling(wrap("head", $1), wrap("body", $3)); }
| row body { $$ = wrap("body", sibling($1, $2)); };

body: row
| body row { $$ = sibling($1, $2); };

row: ROW entries ENTRY { $$ = $2; };

entries: entry { $$ = wrap("row", $1); }
| entries ENTRY entry { $$ = child($1, $3); };

entry: inlines { $$ = wrap("entry", $1); };

/* TODO: Retain ospaces */

codeblock: CODEBLOCK { parser->words = true; } codeblockwords { parser->words = false; } { $$ = content("code", $3); }

codeblockwords: SPACE { $$ = buffer($1.string, $1.length); }
| WORD { $$ = buffer($1.string, $1.length); }
| codeblockwords SPACE { $$ = bappend($1, $2.string, $2.length); }
| codeblockwords WORD { $$ = bappend($1, $2.string, $2.length); }
| codeblockwords CONTINUATION SPACE { $$ = bappend(bappend($1, BAD_CAST "\n", 1), $3.string, $3.length - 4); }
| codeblockwords CONTINUATION WORD { $$ = bappend(bappend($1, BAD_CAST "\n", 1), $3.string, $3.length); };

words: { parser->words = true; } ospace swords ospace { parser->words = false; } { $$ = $3; };

ospace: /* empty */
| SPACE;

swords: WORD { $$ = buffer($1.string, $1.length); }
| swords spaces WORD { $$ = append($1, $3.string, $3.length); };

spaces: spacecontinuation
| spaces spacecontinuation;

spacecontinuation: SPACE
| CONTINUATION;

inlines: ospace sinlines ospace { $$ = $2; };

sinlines: WORD sigils { $$ = word(parser, $1.string, $1.length, $2); }
| anchoredinline
| sinlines WORD sigils { $$ = append_word(parser, $1, $2.string, $2.length, $3); }
| sinlines anchoredinline { $$ = sibling($1, $2); }
| sinlines spaces WORD sigils { $$ = append_spaced_word(parser, $1, $3.string, $3.length, $4); }
| sinlines spaces anchoredinline { $$ = append_inline($1, $3); };

anchoredinline: inline sigils { $$ = anchor(parser, $1, $2); };

inline: CODE { $$ = scontent("code", $1.string, $1.length); }
| EMPHASIS { $$ = scontent("emphasis", $1.string, $1.length); }
| BEGINGROUP sinlines ENDGROUP { $$ = $2; };

sigils: /* empty */ { $$ = NULL; }
| sigil { $$ = xmlListCreate(sigil_free, NULL); xmlListPushBack($$, $1); }
| sigils SIGILSEPARATOR sigil { $$ = $1; xmlListPushBack($$, $3); };

sigil: SIGIL { $$ = sigil_new(&@$, $1.string, $1.length); };

item: { parser->want = INDENT; } inlines oblocks { $$ = children(wrap("item", wrap("p", $2)), $3); };

oblocks: /* empty */ { $$ = NULL; }
| INDENT blocks DEDENT { $$ = $2; };
