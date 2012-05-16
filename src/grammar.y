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
%error-verbose

%token END 0 "end of file"
%token ERROR
%token <substring> WORD
%token <substring> BLANKLINE
%token PARAGRAPH
%token SECTION
%token INDENT
%token DEDENT

%type <buffer> words blanklines paragraphline oblanklines
%type <node> title oblockssections blocks block paragraph sections section

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
        fprintf(stderr, "%s (%d): %s\n", (const char *)parser->p, parser->state, message);
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
buffer_append(xmlNodePtr parent, xmlBufferPtr buffer)
{
        xmlAddChild(parent, xmlNewText(BAD_CAST " "));
        xmlAddChild(parent, xmlNewText(xmlBufferContent(buffer)));
        xmlBufferFree(buffer);
        return parent;
}
}

%%

nmc: title oblockssections { xmlDocSetRootElement(parser->doc, child(child(node("nml"), $1), $2)); };

title: words { $$ = buffer("title", $1); }

words: WORD { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| words WORD { $$ = $1; if ($$) { xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); } };

oblockssections: /* empty */ { $$ = NULL; }
| blanklines blocks { $$ = $2; };
| blanklines blocks blanklines sections { $$ = $2; xmlAddSibling($$, $4); };
| blanklines sections { $$ = $2; };

blocks: block
| blocks blanklines block { $$ = $1; xmlAddSibling($$, $3); };

blanklines: BLANKLINE { $$ = xmlBufferCreate(); xmlBufferAdd($$, $1.string, $1.length); }
| blanklines BLANKLINE { $$ = $1; if ($$) { xmlBufferCCat($$, " "); xmlBufferAdd($$, $2.string, $2.length); } };

block: paragraph;

paragraph: paragraphline { $$ = buffer("p", $1); }
| paragraph paragraphline { $$ = buffer_append($1, $2); };

paragraphline: PARAGRAPH words { $$ = $2; };

sections: section
| sections section { $$ = $1; xmlAddSibling($$, $2); };

section: SECTION title blanklines INDENT blocks oblanklines DEDENT { $$ = child(child(node("section"), $2), $5); };

oblanklines: /* empty */ { $$ = NULL; }
| blanklines;
