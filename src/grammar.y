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

%token END 0 "end of file"
%token ERROR
%token <substring> WORD
%token PARAGRAPH
%token CONTINUATION
%token BLOCKSEPARATOR
%token ENUMERATION
%token SECTION
%token INDENT
%token DEDENT

%type <buffer> words
%type <node> title oblockssections0 oblockssections blockssections blocks block paragraph sections section oblocks enumeration enumerationitem

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
text(const char *name, const xmlChar *content)
{
        xmlNodePtr text = node(name);
        xmlNodeAddContent(text, content);
        return text;
}

static xmlNodePtr
buffer(const char *name, xmlBufferPtr buffer)
{
        xmlNodePtr result = text(name, xmlBufferContent(buffer));
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

title: words { $$ = buffer("title", $1); }

words: WORD { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| words WORD { $$ = $1; xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); };
| words CONTINUATION WORD { $$ = $1; xmlBufferCCat($$, " "); xmlBufferAdd($$, $3.string, $3.length); };

oblockssections0: /* empty */ { $$ = NULL; }
| BLOCKSEPARATOR blockssections { $$ = $2; };

oblockssections: /* empty */ { $$ = NULL; }
| INDENT blockssections DEDENT { $$ = $2; };

blockssections: blocks
| blocks BLOCKSEPARATOR sections { $$ = sibling($1, $3); }
| sections;

blocks: block
| blocks BLOCKSEPARATOR block { $$ = sibling($1, $3); };

block: paragraph
| enumeration;

paragraph: PARAGRAPH words { $$ = buffer("p", $2); };

enumeration: enumerationitem { $$ = child(node("enumeration"), $1); }
| enumeration enumerationitem { $$ = child($1, $2); }

enumerationitem: ENUMERATION { parser->want = INDENT; } words oblocks { $$ = child(child(node("item"), buffer("p", $3)), $4); };

oblocks: /* empty */ { $$ = NULL; }
| INDENT blocks DEDENT { $$ = $2; };

sections: section
| sections section { $$ = sibling($1, $2); };

section: SECTION { parser->want = INDENT; } title oblockssections { $$ = child(child(node("section"), $3), $4); };
