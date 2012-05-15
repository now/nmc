%code top
{
#include <stdio.h>
#include <libxml/tree.h>

#include "grammar.h"
#include "nmc.h"
#include "parser.h"
}

%define api.pure
%parse-param {struct nmc_parser *parser}
%lex-param {struct nmc_parser *parser}
%name-prefix "nmc_grammar_"

%token END 0 "end of file"
%token <substring> WORD;
%token <substring> BLANKLINE;

%type <buffer> words blanklines;
%type <node> title blocks block paragraph;

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
        fputs(message, stderr);
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
}

%%

nmc: title blocks { xmlDocSetRootElement(parser->doc, child(child(node("nml"), $1), $2)); };

title: words { $$ = buffer("title", $1); }

words: WORD { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| words WORD { $$ = $1; if ($$) { xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); } };

blocks: /* empty */ { $$ = NULL; }
| blanklines block { $$ = $2; }
| blocks blanklines block { $$ = $1; xmlAddSibling($$, $3); };

blanklines: BLANKLINE { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| blanklines BLANKLINE { $$ = $1; if ($$) { xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); } };

block: paragraph;

paragraph: words { $$ = buffer("p", $1); };
