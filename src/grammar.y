%code top
{
#include <stdio.h>
#include <stdbool.h>
#include <libxml/tree.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"
}

%define api.pure
%parse-param {struct nmc_parser *parser}
%lex-param {struct nmc_parser *parser}
%name-prefix "nmc_grammar_"
%debug
%error-verbose
%expect 0

%token END 0 "end of file"
%token ERROR
%token <substring> WORD
%token PARAGRAPH
%token SPACE
%token CONTINUATION
%token BLOCKSEPARATOR
%token ITEMIZATION
%token ENUMERATION
%token <substring> DEFINITION
%token QUOTE
%token ATTRIBUTION
%token SEPARATOR
%token ROW
%token ENTRY
%token <substring> CODEBLOCK
%token <substring> FOOTNOTE
%token SECTION
%token INDENT
%token DEDENT
%token <substring> CODE
%token <substring> EMPHASIS
%token BEGINGROUP
%token ENDGROUP

%type <buffer> words swords
%type <string> ospace
%type <node> inlines sinlines inline
%type <node> title oblockssections0 oblockssections blockssections blocks block paragraph sections section oblocks
%type <node> itemization itemizationitem item
%type <node> enumeration enumerationitem
%type <node> definitions definition
%type <node> quote line attribution
%type <node> table tablecontent body row entries entry
%type <node> blockfootnotes blockfootnote
%type <node> footnotedsection

%union {
        const xmlChar *string;
        struct {
                const xmlChar *string;
                int length;
        } substring;
        xmlBufferPtr buffer;
        xmlNodePtr node;
}

%code
{
static int
nmc_grammar_lex(YYSTYPE *value, struct nmc_parser *parser)
{
        return nmc_parser_lex(parser, value);
}

static void
nmc_grammar_error(UNUSED(const struct nmc_parser *parser), const char *message)
{
        fprintf(stderr, "%s: %s\n", (const char *)parser->p, message);
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
wrap(const char *name, xmlNodePtr kid)
{
        return child(node(name), kid);
}

static xmlNodePtr
sibling(xmlNodePtr first, xmlNodePtr last)
{
        if (last == NULL)
                return first;
        xmlAddSibling(first, last);
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
append(xmlBufferPtr buffer, const xmlChar *string, int length)
{
        xmlBufferAdd(buffer, BAD_CAST " ", 1);
        xmlBufferAdd(buffer, string, length);
        return buffer;
}

static xmlNodePtr
footnote(xmlNodePtr blocks, xmlNodePtr footnotes)
{
        xmlNodePtr last = blocks;
        while (last->next != NULL)
                last = last->next;

        if (xmlStrEqual(last->name, BAD_CAST "footnoted")) {
                xmlAddChildList(xmlGetLastChild(last), footnotes->children);
                footnotes->children = NULL;
                xmlFreeNode(footnotes);
                return blocks;
        }

        xmlUnlinkNode(last);
        xmlNodePtr footnoted = child(child(node("footnoted"), last), footnotes);
        return last == blocks ? footnoted : sibling(blocks, footnoted);
}

static xmlNodePtr
prop(xmlNodePtr node, const char *name, const xmlChar *string, int length)
{
        xmlChar *value = xmlStrndup(string, length);
        xmlNewProp(node, BAD_CAST name, value);
        xmlFree(value);
        return node;
}

static xmlNodePtr
codeblock(struct nmc_parser *parser, const xmlChar *string, int length)
{
        const xmlChar *begin = string;
        const xmlChar *p = begin;
        const xmlChar *end = string + length;
        xmlBufferPtr buffer = xmlBufferCreate();

        while (p < end) {
                if (*p == '\n') {
                        xmlBufferAdd(buffer, begin, p - begin + 1);
                        p++;
                        begin = p;
                        for (int i = 0; i < parser->indent + 4; i++, p++) {
                                if (p >= end)
                                        break;
                                if (*p != ' ') {
                                        xmlBufferAdd(buffer, begin, p - begin + 1);
                                        begin = p + 1;
                                        i = -1;
                                }
                        }
                        begin = p;
                }
                p++;
        }
        xmlBufferAdd(buffer, begin, p - begin);

        return content("code", buffer);
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

static xmlNodePtr
wappend(xmlNodePtr inlines, const xmlChar *string, int length)
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
sappend(xmlNodePtr inlines)
{
        return wappend(inlines, BAD_CAST " ", 1);
}


static xmlNodePtr
tappend(xmlNodePtr inlines, const xmlChar *string, int length)
{
        xmlNodeAddContentLen(sappend(inlines), string, length);
        return inlines;
}

static xmlNodePtr
iappend(xmlNodePtr inlines, xmlNodePtr node)
{
        sibling(sappend(inlines), node);
        return inlines;
}

#define YYPRINT(file, type, value) print_token_value(file, type, value)
static void
print_token_value(FILE *file, int type, YYSTYPE value)
{
        switch (type) {
        case WORD:
        case CODE:
        case EMPHASIS:
                fprintf(file, "%.*s", value.substring.length, value.substring.string);
                break;
        case SPACE:
                fputs(" ", file);
                break;
        default:
                break;
        }
}
}

%%

nmc: title oblockssections0 { xmlDocSetRootElement(parser->doc, child(wrap("nml", $1), $2)); };

title: words { $$ = content("title", $1); }

words: ospace swords ospace { $$ = $2; };

ospace: /* empty */ { $$ = BAD_CAST ""; }
| SPACE { $$ = BAD_CAST " "; };

swords: WORD { $$ = buffer($1.string, $1.length); }
| swords SPACE WORD { $$ = append($1, $3.string, $3.length); }
| swords CONTINUATION WORD { $$ = append($1, $3.string, $3.length); };

inlines: ospace sinlines ospace { $$ = $2; };

sinlines: WORD { $$ = xmlNewTextLen($1.string, $1.length); }
| inline
| sinlines WORD { $$ = $1; wappend($1, $2.string, $2.length); }
| sinlines inline { $$ = sibling($1, $2); }
| sinlines SPACE WORD { $$ = tappend($1, $3.string, $3.length); }
| sinlines SPACE inline { $$ = iappend($1, $3); }
| sinlines CONTINUATION WORD { $$ = tappend($1, $3.string, $3.length); };
| sinlines CONTINUATION inline { $$ = iappend($1, $3); };

inline: CODE { $$ = scontent("code", $1.string, $1.length); }
| EMPHASIS { $$ = scontent("emphasis", $1.string, $1.length); }
| BEGINGROUP sinlines ENDGROUP { $$ = $2; };

oblockssections0: /* empty */ { $$ = NULL; }
| BLOCKSEPARATOR blockssections { $$ = $2; };

oblockssections: /* empty */ { $$ = NULL; }
| INDENT blockssections DEDENT { $$ = $2; };

blockssections: blocks
| blocks BLOCKSEPARATOR sections { $$ = sibling($1, $3); }
| sections;

blocks: block
| blocks BLOCKSEPARATOR block { $$ = sibling($1, $3); }
| blocks BLOCKSEPARATOR blockfootnotes { $$ = footnote($1, $3); };

block: paragraph
| itemization
| enumeration
| definitions
| quote
| table
| CODEBLOCK { $$ = codeblock(parser, $1.string, $1.length); };

blockfootnotes: blockfootnote { $$ = wrap("footnotes", $1); }
| blockfootnotes blockfootnote { $$ = child($1, $2); };

blockfootnote: FOOTNOTE words { $$ = prop(content("footnote", $2), "id", $1.string, $1.length); };

paragraph: PARAGRAPH inlines { $$ = wrap("p", $2); };

itemization: itemizationitem { $$ = wrap("itemization", $1); }
| itemization itemizationitem { $$ = child($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

item: { parser->want = INDENT; } inlines oblocks { $$ = child(wrap("item", wrap("p", $2)), $3); };

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

table: tablecontent { $$ = wrap("table", $1); }

tablecontent: row { $$ = wrap("body", $1); }
| row SEPARATOR body { $$ = sibling(wrap("head", $1), wrap("body", $3)); }
| row body { $$ = wrap("body", sibling($1, $2)); };

body: row
| body row { $$ = sibling($1, $2); };

row: ROW entries ENTRY { $$ = $2; };

entries: entry { $$ = wrap("row", $1); }
| entries ENTRY entry { $$ = child($1, $3); };

entry: inlines { $$ = wrap("entry", $1); };

oblocks: /* empty */ { $$ = NULL; }
| INDENT blocks DEDENT { $$ = $2; };

sections: footnotedsection
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section blockfootnotes { $$ = footnote($1, $2); };

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = child(wrap("section", $3), $4); };
