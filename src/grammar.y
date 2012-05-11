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

%token <substring> WORD;

%type <buffer> title;

%union {
        const xmlChar *string;
        struct {
                const xmlChar *string;
                int length;
        } substring;
        xmlBufferPtr buffer;
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
        xmlAddChild(parent, child);
        return parent;
}
}

%%

nmc: title { xmlDocSetRootElement(parser->doc, child(node("nml"), buffer("title", $1))); };

title: WORD { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| title WORD { $$ = $1; if ($$){ xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); } };
