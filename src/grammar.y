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
%token CONTINUATION
%token BLOCKSEPARATOR
%token ITEMIZATION
%token ENUMERATION
%token QUOTE
%token ATTRIBUTION
%token <substring> CODEBLOCK
%token <substring> FOOTNOTE
%token SECTION
%token INDENT
%token DEDENT
%token <substring> CODE

%type <buffer> words
%type <node> inlines
%type <node> inline
%type <node> title oblockssections0 oblockssections blockssections blocks block paragraph sections section oblocks
%type <node> itemization itemizationitem item
%type <node> enumeration enumerationitem
%type <node> quote line attribution
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
child(xmlNodePtr parent, xmlNodePtr child)
{
        if (child == NULL)
                return parent;
        xmlAddChild(parent, child);
        return parent;
}

static xmlNodePtr
sibling(xmlNodePtr first, xmlNodePtr last)
{
        if (last == NULL)
                return last;
        xmlAddSibling(first, last);
        return first;
}

static xmlBufferPtr
buffer(const xmlChar *string, int length)
{
        xmlBufferPtr buffer = xmlBufferCreate();
        const xmlChar *p = string;
        while (*p == ' ')
                p++;
        xmlBufferAdd(buffer, p, length - (p - string));
        return buffer;
}

static xmlBufferPtr
append(xmlBufferPtr buffer, const xmlChar *string, int length)
{
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
text(const xmlChar *string, int length)
{
        const xmlChar *p = string;
        while (*p == ' ')
                p++;
        return xmlNewTextLen(p, length - (p - string));
}

static xmlNodePtr
tappend(xmlNodePtr inlines, const xmlChar *string, int length)
{
        xmlNodePtr last = inlines;
        while (last->next != NULL)
                last = last->next;

        if (xmlNodeIsText(last))
                xmlNodeAddContentLen(last, string, length);
        else
                sibling(inlines, xmlNewTextLen(string, length));

        return inlines;
}

#define YYPRINT(file, type, value) print_token_value(file, type, value)
static void
print_token_value(FILE *file, int type, YYSTYPE value)
{
        if (type == WORD)
                fprintf(file, "%.*s", value.substring.length, value.substring.string);
}
}

%%

nmc: title oblockssections0 { xmlDocSetRootElement(parser->doc, child(child(node("nml"), $1), $2)); };

title: words { $$ = content("title", $1); }

words: WORD { $$ = buffer($1.string, $1.length); }
| words WORD { $$ = append($1, $2.string, $2.length); }
| words CONTINUATION WORD { $$ = append(append($1, BAD_CAST " ", 1), $3.string, $3.length); };

inlines: WORD { $$ = text($1.string, $1.length); }
| inline
| inlines WORD { $$ = tappend($1, $2.string, $2.length); }
| inlines inline { $$ = sibling($1, $2); }
| inlines CONTINUATION WORD { $$ = tappend(tappend($1, BAD_CAST " ", 1), $3.string, $3.length); };
| inlines CONTINUATION inline { $$ = sibling(tappend($1, BAD_CAST " ", 1), $3); };

inline: CODE { $$ = node("code"); xmlNodeAddContentLen($$, $1.string, $1.length); };

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
| quote
| CODEBLOCK { $$ = codeblock(parser, $1.string, $1.length); };

blockfootnotes: blockfootnote { $$ = child(node("footnotes"), $1); }
| blockfootnotes blockfootnote { $$ = child($1, $2); };

blockfootnote: FOOTNOTE words { $$ = prop(content("footnote", $2), "id", $1.string, $1.length); };

paragraph: PARAGRAPH inlines { $$ = child(node("p"), $2); };

itemization: itemizationitem { $$ = child(node("itemization"), $1); }
| itemization itemizationitem { $$ = child($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

item: { parser->want = INDENT; } words oblocks { $$ = child(child(node("item"), content("p", $2)), $3); };

enumeration: enumerationitem { $$ = child(node("enumeration"), $1); }
| enumeration enumerationitem { $$ = child($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

quote: line { $$ = child(node("quote"), $1); }
| quote line { $$ = child($1, $2); }
| quote attribution { $$ = child($1, $2); };

line: QUOTE inlines { $$ = child(node("line"), $2); };

attribution: ATTRIBUTION inlines { $$ = child(node("attribution"), $2); };

oblocks: /* empty */ { $$ = NULL; }
| INDENT blocks DEDENT { $$ = $2; };

sections: footnotedsection
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section blockfootnotes { $$ = footnote($1, $2); };

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = child(child(node("section"), $3), $4); };
