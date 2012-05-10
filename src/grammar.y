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

%token <string> TITLE;

%union {
        const xmlChar *string;
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
child(xmlNodePtr parent, xmlNodePtr child)
{
        xmlAddChild(parent, child);
        return parent;
}
}

%%

nmc: TITLE { xmlDocSetRootElement(parser->doc, child(node("nml"), text("title", $1))); }
