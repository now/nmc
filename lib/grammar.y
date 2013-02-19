%{
#include <config.h>

#include <assert.h>
#include <sys/types.h>
#include <regex.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nmc.h>
#include <nmc/list.h>

#include <private.h>

#include "buffer.h"
#include "error.h"
#include "unicode.h"

#define YYLTYPE struct nmc_location

struct substring {
        const char *string;
        size_t length;
};

struct nodes {
        struct nmc_node *first;
        struct nmc_node *last;
};

struct parser {
        const char *p;
        YYLTYPE location;
        size_t indent;
        size_t dedents;
        bool bol;
        int want;
        struct nmc_node *doc;
        struct buffer buffer;
        struct buffer_node *buffer_node;
        struct anchor *anchors;
        struct {
                struct nmc_parser_error *first;
                struct nmc_parser_error *last;
        } errors;
};

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

static char *
mstrdup(const char *source, size_t n)
{
        char *target = malloc(n + 1);
        if (target == NULL)
                return NULL;
        target[n] = '\0';
        return memcpy(target, source, n);
}

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

struct footnote {
        struct footnote *next;
        YYLTYPE location;
        struct id id;
        struct nmc_data_node *node;
};

static void
footnote_free1(struct footnote *footnote)
{
        free(footnote->id.string);
        nmc_node_free((struct nmc_node *)footnote->node);
        free(footnote);
}

static void
footnote_free(struct footnote *footnote)
{
        list_for_each_safe(struct footnote, p, n, footnote)
                footnote_free1(p);
}

static void nmc_node_unlink_and_free(struct nmc_node *node, struct parser *parser);
%}

%define api.pure full
%parse-param {struct parser *parser}
%lex-param {struct parser *parser}
%name-prefix "nmc_grammar_"
%debug
%error-verbose
%expect 0
%locations

%token END 0 "end of file"
%token ERROR
%token AGAIN
%token <substring> WORD
%token PARAGRAPH "paragraph tag (‘ ’)"
%token <substring> SPACE
%token ITEMIZATION "itemization tag (‘•’)"
%token ENUMERATION "enumeration tag (‘₁’, ‘₂’, …)"
%token <node> TERM "term tag (‘=’)"
%token <node> FIGURE "figure tag (“Fig.”)"
%token QUOTE "quote tag (‘>’)"
%token ATTRIBUTION "attribution tag (‘—’)"
%token TABLESEPARATOR "table separator tag (“|-”)"
%token ROW "table tag (‘|’)"
%token CELLSEPARATOR "cell separator (‘|’)"
%token <node> CODEBLOCK "code block"
%token <footnote> FOOTNOTE "footnote"
%token SECTION "section tag (‘§’)"
%token INDENT
%token ITEMINDENT
%token DEDENT
%token <node> CODE "code inline (‹…›)"
%token <node> EMPHASIS "emphasized text (/…/)"
%token <node> ANCHOR "footnote anchor (¹, ², …)"
%token ANCHORSEPARATOR "footnote anchor separator (⁺)"
%token BEGINGROUP "beginning of grouped text ({…)"
%token ENDGROUP "end of grouped text (…})"

%left NotFootnote
%left FOOTNOTE

%left NotBlock
%left ITEMIZATION
%left ENUMERATION
%left TERM
%left ATTRIBUTION
%left QUOTE
%left ROW

%type <node> documenttitle
%type <nodes> words
%type <nodes> oblockssections0 blockssections0 sections0
%type <nodes> blockssections blocks sections oblockssections
%type <node> block footnotedsection section title
%type <footnote> footnotes
%type <node> itemizationitem
%type <nodes> itemizationitems
%type <node> enumerationitem
%type <nodes> enumerationitems
%type <node> definition
%type <nodes> definitionitems
%type <node> line attribution
%type <nodes> quotecontent lines
%type <node> head body row cell
%type <nodes> headbody rows cells
%type <nodes> figure
%type <nodes> inlines sinlines
%type <node> oanchoredinline anchoredinline inline
%type <node> item firstparagraph
%type <nodes> oblocks

%union {
        struct substring substring;
        struct nodes nodes;
        struct nmc_node *node;
        struct footnote *footnote;
}

%printer { fprintf(yyoutput, "%.*s", (int)$$.length, $$.string); } <substring>
%printer {
        if ($$.first != NULL) {
                fprintf(yyoutput, "first: %d", $$.first->name);
                if ($$.last != NULL)
                        fprintf(yyoutput, ", last: %d", $$.last->name);
        }
} <nodes>
%printer { fprintf(yyoutput, "%d", $$->name); } <node>
%printer {
        if ($$->node != NULL)
                fprintf(yyoutput, "%s %d", $$->id.string, $$->node->node.node.name);
        else
                fprintf(yyoutput, "%s (unrecognized)", $$->id.string);
} <footnote>

%destructor { nmc_node_unlink_and_free($$.first, parser); } <nodes>
%destructor { nmc_node_unlink_and_free($$, parser); } <node>
%destructor { footnote_free($$); } <footnote>

%code
{
static inline bool
parser_is_oom(struct parser *parser)
{
        return parser->errors.last == &nmc_parser_oom_error;
}

static void
parser_errors(struct parser *parser,
              struct nmc_parser_error *first, struct nmc_parser_error *last)
{
        if (parser_is_oom(parser)) {
                nmc_parser_error_free(first);
                return;
        }
        if (parser->errors.first == NULL)
                parser->errors.first = first;
        if (parser->errors.last != NULL)
                parser->errors.last->next = first;
        if (last != NULL)
                parser->errors.last = last;
}

static void
parser_oom(struct parser *parser)
{
        if (parser_is_oom(parser))
                return;
        nmc_parser_oom_error.location = parser->location;
        parser_errors(parser, &nmc_parser_oom_error, &nmc_parser_oom_error);
}

static bool
parser_errorv(struct parser *parser, YYLTYPE *location,
              const char *message, va_list args)
{
        if (parser_is_oom(parser))
                return false;
        struct nmc_parser_error *error = nmc_parser_error_newv(location, message, args);
        if (error == NULL) {
                parser_oom(parser);
                return false;
        }
        parser_errors(parser, error, error);
        return true;
}

static bool NMC_PRINTF(3, 4)
parser_error(struct parser *parser, YYLTYPE *location,
             const char *message, ...)
{
        va_list args;
        va_start(args, message);
        bool r = parser_errorv(parser, location, message, args);
        va_end(args);
        return r;
}

static void
locate(struct parser *parser, YYLTYPE *location, int last_column)
{
        parser->location.last_column = last_column;
        *location = parser->location;
}

static int
token(struct parser *parser, YYLTYPE *location, const char *end, int type)
{
        if (location != NULL)
                locate(parser,
                       location,
                       parser->location.first_column +
                       (end - parser->p - (end == parser->p ? 0 : 1)));

        parser->location.first_line = parser->location.last_line;
        parser->location.last_column++;
        parser->location.first_column = parser->location.last_column;
        parser->p = end;
        return type;
}

static int
substring(struct parser *parser, YYLTYPE *location, YYSTYPE *value,
          const char *end, int type)
{
        value->substring.string = parser->p;
        value->substring.length = end - value->substring.string;
        return token(parser, location, end, type);
}

static int
dedent(struct parser *parser, YYLTYPE *location, const char *end)
{
        parser->dedents--;
        return token(parser, location, end, DEDENT);
}

static int
dedents(struct parser *parser, const char *begin, size_t spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        return dedent(parser, NULL, begin + parser->indent);
}

typedef bool (*isfn)(uchar);

static inline size_t
length_of_run(const char *p, isfn is)
{
        const char *end = p;
        while (is(u_dref(end)))
                end = u_next(end);
        return end - p;
}

#define U_SUPERSCRIPT_0 ((uchar)0x2070)
#define U_SUPERSCRIPT_1 ((uchar)0x00b9)
#define U_SUPERSCRIPT_2 ((uchar)0x00b2)
#define U_SUPERSCRIPT_3 ((uchar)0x00b3)
#define U_SUPERSCRIPT_4 ((uchar)0x2074)
#define U_SUPERSCRIPT_9 ((uchar)0x2079)

static inline bool
is_superscript(uchar c)
{
        switch (c) {
        case U_SUPERSCRIPT_0:
        case U_SUPERSCRIPT_1:
        case U_SUPERSCRIPT_2:
        case U_SUPERSCRIPT_3:
                return true;
        default:
                return U_SUPERSCRIPT_4 <= c && c <= U_SUPERSCRIPT_9;
        }
}

static inline size_t
superscript(const char *p)
{
        return length_of_run(p, is_superscript);
}

#define U_SUBSCRIPT_0 ((uchar)0x2080)
#define U_SUBSCRIPT_9 ((uchar)0x2089)

static inline bool
is_subscript(uchar c)
{
        return U_SUBSCRIPT_0 <= c && c <= U_SUBSCRIPT_9;
}

static inline size_t
subscript(const char *p)
{
        return length_of_run(p, is_subscript);
}

static inline bool
is_end(const char *end)
{
        return *end == '\0' || *end == '\n';
}

static inline bool
is_space_or_end(const char *end)
{
        return is_end(end) || *end == ' ';
}

static char *
text(struct parser *parser, YYLTYPE *location, const char *begin)
{
        while (*begin == ' ')
                begin++;
        const char *sbegin = parser->p;
        const char *end = begin;
        struct buffer b = BUFFER_INIT;

again:
        switch (*end) {
        case '\0':
                break;
        case '\n': {
                const char *send = end + 1;
                while (*send == ' ')
                        send++;
                if (!is_end(send) &&
                    (size_t)(send - (end + 1)) >= parser->indent + 2) {
                        if (!buffer_append(&b, begin, end - begin))
                                goto oom;
                        sbegin = end + 1;
                        begin = send - 1;
                        end = send;
                        parser->location.last_line++;
                        goto again;
                }
                break;
        }
        case ' ':
                end++;
                if (!buffer_append(&b, begin, end - begin))
                        goto oom;
                while (*end == ' ')
                        end++;
                begin = end;
                goto again;
        default:
                end++;
                goto again;
        }
        if (!buffer_append(&b, begin, end - begin))
                goto oom;

        locate(parser, location, end - sbegin);
        // NOTE We use a throwaway type here; caller must return actual type.
        token(parser, NULL, end, ERROR);
        return buffer_str(&b);
oom:
        locate(parser, location, end - sbegin);
        token(parser, NULL, end, ERROR);
        free(b.content);
        return NULL;
}

static size_t
bol_space(struct parser *parser, size_t offset)
{
        if (*(parser->p + offset) == ' ')
                return offset + 1;
        YYLTYPE location = parser->location;
        location.first_column += offset;
        location.last_column = location.first_column;
        parser_error(parser, &location,
                     "expected ‘ ’ after “%.*s”", (int)offset, parser->p);
        return offset;
}

typedef struct nmc_data_node *(*definefn)(const char *, regmatch_t *);

struct definition {
        struct definition *next;
        regex_t regex;
        definefn define;
};

static struct definition *definitions;

static char *
aregerror(int errcode, const regex_t *regex)
{
        size_t n = regerror(errcode, regex, NULL, 0);
        char *s = malloc(n);
        if (s == NULL)
                return NULL;
        regerror(errcode, regex, s, n);
        return s;
}

static bool
definitions_push(const char *pattern, definefn define, struct nmc_error *error)
{
        struct definition *definition = malloc(sizeof(struct definition));
        if (definition == NULL)
                return nmc_error_oom(error);
        int r = regcomp(&definition->regex, pattern, REG_EXTENDED);
        if (r != 0) {
                char *s = aregerror(r, &definition->regex);
                free(definition);
                if (s == NULL)
                        return nmc_error_oom(error);
                nmc_error_format(error, -1,
                                 "definition regex compilation failed: %s", s);
                free(s);
                return false;
        }
        definition->define = define;
        definition->next = definitions;
        definitions = definition;
        return true;
}

static inline struct nmc_node *
node_init(struct nmc_node *node, enum nmc_node_type type, enum nmc_node_name name)
{
        if (node == NULL)
                return NULL;
        node->next = NULL;
        node->type = type;
        node->name = name;
        return node;
}
#define node_new(stype, type, name) ((stype *)node_init(malloc(sizeof(stype)), type, name))

static struct nmc_data_node *
data_node_new(enum nmc_node_name name, size_t n)
{
        struct nmc_data_node *d = node_new(struct nmc_data_node,
                                           NMC_NODE_TYPE_DATA, name);
        if (d == NULL)
                return NULL;
        d->node.children = NULL;
        d->data = malloc(sizeof(struct nmc_node_data) +
                         sizeof(struct nmc_node_datum) * (n + 1));
        if (d->data == NULL) {
                free(d);
                return NULL;
        }
        d->data->references = 1;
        d->data->data[n].name = NULL;
        d->data->data[n].value = NULL;
        return d;
}

static void
node_data_free(struct nmc_node_data *data)
{
        if (--data->references > 0)
                return;
        for (struct nmc_node_datum *d = data->data; d->value != NULL; d++)
                free(d->value);
        free(data);
}

static struct nmc_data_node *
data_node_add_matchesv(struct nmc_data_node *node,
                       const char *buffer, regmatch_t *matches, int n,
                       va_list args)
{
        if (node == NULL)
                return NULL;
        regmatch_t *m = &matches[1];
        struct nmc_node_datum *d = node->data->data;
        for (int i = 0; i < n; i++) {
                assert(m->rm_so != -1);
                assert(m->rm_eo != -1);
                d->name = va_arg(args, const char *);
                d->value = mstrdup(buffer + m->rm_so, m->rm_eo - m->rm_so);
                if (d->value == NULL) {
                        node_data_free(node->data);
                        free(node);
                        return NULL;
                }
                m++;
                d++;
        }
        return node;
}

static struct nmc_data_node *
data_node_add_matches(struct nmc_data_node *node,
                      const char *buffer, regmatch_t *matches, int n, ...)
{
        va_list args;
        va_start(args, n);
        struct nmc_data_node *r =
                data_node_add_matchesv(node, buffer, matches, n, args);
        va_end(args);
        return r;
}

static struct nmc_data_node *
data_node_new_matches(enum nmc_node_name name,
                      const char *buffer, regmatch_t *matches, int n, ...)
{
        va_list args;
        va_start(args, n);
        struct nmc_data_node *r =
                data_node_add_matchesv(data_node_new(name, n),
                                       buffer, matches, n, args);
        va_end(args);
        return r;
}

static struct nmc_data_node *
data_node_new_link(size_t n, const char *buffer, regmatch_t *matches)
{
        return data_node_add_matches(data_node_new(NMC_NODE_LINK, n),
                                     buffer, matches, 2, "title", "uri");
}

static struct nmc_data_node *
abbreviation(const char *buffer, regmatch_t *matches)
{
        return data_node_new_matches(NMC_NODE_ABBREVIATION, buffer, matches,
                                     1, "for");
}

static struct nmc_data_node *
inline_figure(const char *buffer, regmatch_t *matches)
{
        bool alternate = matches[3].rm_so != -1;
        struct nmc_data_node *d =
                data_node_new_link(3 + (alternate ? 1 : 0), buffer, matches);
        if (d == NULL)
                return NULL;
        d->data->data[2].name = "relation";
        d->data->data[2].value = mstrdup("figure", 6);
        if (d->data->data[2].value == NULL) {
                node_data_free(d->data);
                free(d);
                return NULL;
        }
        if (!alternate)
                return d;
        assert(matches[4].rm_so != -1);
        assert(matches[4].rm_eo != -1);
        d->data->data[3].name = "relation-data";
        d->data->data[3].value = mstrdup(buffer + matches[4].rm_so,
                                         matches[4].rm_eo - matches[4].rm_so);
        if (d->data->data[3].value == NULL) {
                node_data_free(d->data);
                free(d);
                return NULL;
        }
        return d;
}

static struct nmc_data_node *
link(const char *buffer, regmatch_t *matches)
{
        return data_node_new_link(2, buffer, matches);
}

static bool
definitions_init(struct nmc_error *error)
{
        if (definitions != NULL)
                return true;
        return  definitions_push("^(.+) +at +([^ ]+)", link, error) &&
                definitions_push("^(.+), +see +([^ ]+)( +\\((.+)\\))?", inline_figure, error) &&
                definitions_push("^Abbreviation +for +(.+)", abbreviation, error);
}

static void
definitions_free(void)
{
        list_for_each_safe(struct definition, p, n, definitions) {
                regfree(&p->regex);
                free(p);
        }
        definitions = NULL;
}

static struct nmc_data_node *
define(YYLTYPE *location, const char *content, struct nmc_parser_error **error)
{
        list_for_each(struct definition, p, definitions) {
                regmatch_t matches[p->regex.re_nsub + 1];
                int r = regexec(&p->regex, content,
                                p->regex.re_nsub + 1, matches, 0);
                if (r == 0)
                        return p->define(content, matches);
                else if (r != REG_NOMATCH) {
                        char *s = aregerror(r, &p->regex);
                        if (s == NULL) {
                                *error = &nmc_parser_oom_error;
                                return NULL;
                        }
                        *error = nmc_parser_error_new(location,
                                                      "footnote definition regex execution failed: %s",
                                                      s);
                        free(s);
                        if (*error == NULL)
                                *error = &nmc_parser_oom_error;
                        return NULL;
                }
        }
        *error = nmc_parser_error_new(location, "unrecognized footnote content: %s", content);
        return NULL;
}

static int
footnote(struct parser *parser, YYLTYPE *location, YYSTYPE *value, size_t length)
{
        value->footnote = malloc(sizeof(struct footnote));
        if (value->footnote == NULL)
                return FOOTNOTE;
        value->footnote->next = NULL;
        char *id = mstrdup(parser->p, length);
        if (id == NULL)
                goto oom;
        value->footnote->id = id_new(id);
        char *content = text(parser, location, parser->p + bol_space(parser, length));
        if (content == NULL)
                goto oom_id;
        struct nmc_parser_error *error = NULL;
        value->footnote->node = define(location, content, &error);
        free(content);
        if (value->footnote->node == NULL) {
                if (error == NULL)
                        goto oom_id;
                parser_errors(parser, error, error);
        }
        value->footnote->location = *location;
        return FOOTNOTE;
oom_id:
        free(id);
oom:
        free(value->footnote);
        value->footnote = NULL;
        return FOOTNOTE;
}

static struct nmc_node *
text_node_new(enum nmc_node_name name, char *text)
{
        if (text == NULL)
                return NULL;
        struct nmc_text_node *n = node_new(struct nmc_text_node,
                                           NMC_NODE_TYPE_TEXT, name);
        if (n == NULL) {
                free(text);
                return NULL;
        }
        n->text = text;
        return (struct nmc_node *)n;
}

static int
codeblock(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *sbegin = parser->p;
        const char *begin = parser->p + 4;
        const char *end = begin;
        struct buffer b = BUFFER_INIT;

        while (*end != '\0') {
                while (!is_end(end))
                        end++;
                if (!buffer_append(&b, begin, end - begin))
                        goto oom;
                if (*end != '\n')
                        break;
                size_t lines = 1;
                sbegin = end + 1;
                const char *send = sbegin;
                while (*send == ' ' || *send == '\n') {
                        if (*send == '\n') {
                                lines++;
                                sbegin = send + 1;
                        }
                        send++;
                }
                if ((size_t)(send - sbegin) < parser->indent + 4)
                        break;
                begin = sbegin + parser->indent + 4;
                end = send;
                parser->location.last_line += lines;
                if (!buffer_append_c(&b, '\n', lines))
                        goto oom;
        }

        value->node = text_node_new(NMC_NODE_CODEBLOCK, buffer_str(&b));
        goto done;
oom:
        value->node = NULL;
done:
        locate(parser, location, end - sbegin);
        return token(parser, NULL, end, CODEBLOCK);
}

static struct nmc_node *
text_node_new_dup(enum nmc_node_name name, const char *string, size_t length)
{
        return text_node_new(name, mstrdup(string, length));
}

static int NMC_PRINTF(5, 6)
error_token(struct parser *parser, YYLTYPE *location, const char *end, int type,
            const char *message, ...)
{
        va_list args;
        va_start(args, message);
        int r = token(parser, location, end, type);
        parser_errorv(parser, &parser->location, message, args);
        va_end(args);
        return r;
}

static int
term(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + bol_space(parser, 1);
        const char *end = begin;

        while (!is_end(end)) {
                if (*end == '.') {
                        const char *send = end;
                        end++;
                        while (*end == ' ')
                                end++;
                        if (*end == '=' && is_space_or_end(end + 1)) {
                                value->node = text_node_new_dup(NMC_NODE_TERM, begin, send - begin);
                                return token(parser, location, end + 1, TERM);
                        }
                } else
                        end++;
        }

        return error_token(parser, location, end, AGAIN,
                           "expected “. =” after term in definition");
}

static const char *
skip_spaces_and_empty_lines(struct parser *parser, const char **begin, const char *end)
{
        while (true) {
                while (*end == ' ')
                        end++;
                if (*end != '\n')
                        break;
                end++;
                parser->location.last_line++;
                *begin = end;
        }
        return end;
}

static const char *
figure_alternate(struct parser *parser, const char **begin, const char **end)
{
        const char *p = *end;
        if (*p != '\n')
                return NULL;
        p++;
        parser->location.last_line++;
        *begin = p;
        while (*p == ' ')
                p++;
        if (*p != '(') {
                *end = p;
                return NULL;
        }
        p++;
        size_t nesting = 1;
        const char *middle = p;
        while (true) {
                switch (*p) {
                case '(':
                        nesting++;
                        break;
                case ')':
                        if (--nesting == 0)
                                goto done;
                        break;
                case '\n':
                case '\0':
                        goto done;
                }
                p++;
        }
done:
        *end = p;
        return middle;
}

static int
figure(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        value->node = NULL;

        const char *begin = parser->p;
        const char *end = parser->p + 4;
        switch (*end) {
        case '\n':
                parser->location.last_line++;
                begin = end + 1;
                // Fall through
        case ' ':
                end++;
                break;
        default:
                if (!parser_error(parser, &parser->location,
                                  "expected ‘ ’ or newline after figure tag (“Fig.”)"))
                        goto oom;
        }

        end = skip_spaces_and_empty_lines(parser, &begin, end);
        if (*end == '\0') {
                locate(parser, location, end - begin);
                return error_token(parser, NULL, end, END,
                                   "expected URI for figure image");
        }
        const char *middle = end;
        while (!is_end(end) && *end != ' ')
                end++;
        char *uri = mstrdup(middle, end - middle);
        if (uri == NULL)
                goto oom;
        while (*end == ' ')
                end++;
        struct nmc_node *alternate = NULL;
        middle = figure_alternate(parser, &begin, &end);
        if (middle != NULL) {
                bool terminated = *end == ')';
                if (!terminated) {
                        YYLTYPE l;
                        locate(parser, &l, end + 1 - begin);
                        l.first_line = l.last_line;
                        l.first_column = l.last_column;
                        if (!parser_error(parser, &l,
                                          "expected ‘)’ after figure image alternate text")) {
                                free(uri);
                                goto oom;
                        }
                }
                alternate = text_node_new_dup(NMC_NODE_TEXT, middle, end - middle);
                if (alternate == NULL) {
                        free(uri);
                        goto oom;
                }
                if (terminated)
                        end++;
        }
        struct nmc_data_node *n = data_node_new(NMC_NODE_IMAGE, 1);
        if (n == NULL) {
                free(uri);
                nmc_node_free(alternate);
                goto oom;
        }
        n->data->data[0].name = "uri";
        n->data->data[0].value = uri;
        value->node = (struct nmc_node *)n;
        n->node.children = alternate;
oom:
        end = skip_spaces_and_empty_lines(parser, &begin, end);
        locate(parser, location, end - begin);
        return token(parser, NULL, end, FIGURE);
}

static char *token_name(int type);
static size_t token_name_unescape(char *result, const char *escaped);
#define yytnamerr token_name_unescape

static int
bol_token(struct parser *parser, YYLTYPE *location, size_t length, int type)
{
        const char *end = parser->p + length;
        if (*end != ' ') {
                char *name = token_name(type);
                if (name != NULL) {
                        int r = error_token(parser, location, end, type,
                                            "expected ‘ ’ after %s", name);
                        free(name);
                        return r;
                } else
                        parser_oom(parser);
        }
        return token(parser, location, end + 1, type);
}

#define U_PILCROW_SIGN ((uchar)0x00b6)
#define U_SECTION_SIGN ((uchar)0x00a7)
#define U_BULLET ((uchar)0x2022)
#define U_EM_DASH ((uchar)0x2014)

static bool
is_bol_symbol(const char *end)
{
        uchar c = u_dref(end);
        switch (c) {
        case U_PILCROW_SIGN:
        case U_SECTION_SIGN:
        case U_BULLET:
        case U_EM_DASH:
        case '=':
        case '>':
        case '|':
                return true;
        case 'F':
                return end[1] == 'i' && end[2] == 'g' && end[3] == '.';
        default:
                return is_subscript(c) || is_superscript(c);
        }
}

static int
bol_item(struct parser *parser, YYLTYPE *location, size_t length, int type)
{
        const char *begin = parser->p + length;
        const char *end = begin;
        if (*end != ' ' || *++end != ' ' || *++end != ' ') {
                char *name = token_name(type);
                int r = error_token(parser, location, end, type,
                                    "expected “%*s” after %s",
                                    (int)(3 - (end - begin)), "", name);
                free(name);
                return r;
        }
        return token(parser, location, end, type);
}

static int
bol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->bol = false;

        size_t length;
        if ((length = subscript(parser->p)) > 0)
                return bol_item(parser, location, length, ENUMERATION);
        else if ((length = superscript(parser->p)) > 0)
                return footnote(parser, location, value, length);

        uchar c = u_lref(parser->p, &length);
        switch (c) {
        case ' ':
                if (*(parser->p + 1) == ' ') {
                        if (*(parser->p + 2) == ' ' && *(parser->p + 3) == ' ')
                                return codeblock(parser, location, value);
                        return token(parser, location, parser->p + 2, PARAGRAPH);
                }
                return bol_token(parser, location, length, PARAGRAPH);
        case U_PILCROW_SIGN:
                return bol_token(parser, location, length, PARAGRAPH);
        case U_SECTION_SIGN:
                return bol_token(parser, location, length, SECTION);
        case U_BULLET:
                return bol_item(parser, location, length, ITEMIZATION);
        case U_EM_DASH:
                return bol_token(parser, location, length, ATTRIBUTION);
        case '=':
                return term(parser, location, value);
        case '>':
                return bol_token(parser, location, length, QUOTE);
        case '|':
                if (*(parser->p + 1) == '-') {
                        const char *end = parser->p + 2;
                        while (!is_end(end))
                                end++;
                        return token(parser, location, end, TABLESEPARATOR);
                }
                return bol_token(parser, location, length, ROW);
        case 'F':
                if (parser->p[1] == 'i' && parser->p[2] == 'g' &&
                    parser->p[3] == '.')
                        return figure(parser, location, value);
        case '\0':
                return token(parser, location, parser->p, END);
        }

        if (c == U_BAD_INPUT_CHAR)
                parser_error(parser, &parser->location,
                             "broken UTF-8 sequence starting with %#02x",
                             *parser->p);
        else if (!uc_issolid(c))
                parser_error(parser, &parser->location,
                             "unrecognized tag character U+%04X",
                             c);
        else
                parser_error(parser, &parser->location,
                             "unrecognized tag character ‘%.*s’ (U+%04X)",
                             (int)length, parser->p, c);

        return PARAGRAPH;
}

static int
eol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->location.last_line++;
        parser->location.first_line = parser->location.last_line;
        parser->location.first_column = 1;

        const char *begin = parser->p + 1;
        const char *end = begin;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                parser->location.last_line++;
                begin = end;
                end = skip_spaces_and_empty_lines(parser, &begin, end);
                size_t spaces = end - begin;
                if ((parser->want == INDENT && spaces >= parser->indent + 2) ||
                    (parser->want == ITEMINDENT && spaces >= parser->indent + 2 &&
                     (spaces > parser->indent + 2 || is_bol_symbol(end)))) {
                        int want = parser->want;
                        parser->want = ERROR;
                        parser->indent += 2;
                        locate(parser, location, parser->indent);
                        return token(parser, NULL, begin + parser->indent, want);
                } else if (spaces < parser->indent && spaces % 2 == 0) {
                        parser->want = ERROR;
                        locate(parser, location, spaces);
                        return dedents(parser, begin, spaces);
                } else if (parser->indent > 0 && spaces == parser->indent &&
                           !is_bol_symbol(end)) {
                        // EXAMPLE An itemization containing multiple
                        // paragraphs followed by a paragraph.
                        parser->want = ERROR;
                        parser->location.first_line = parser->location.last_line;
                        locate(parser, location, spaces + 1);
                        return dedents(parser, begin, spaces - 2);
                } else {
                        parser->want = ERROR;
                        parser->location.first_line = parser->location.last_line;
                        locate(parser, location, parser->indent + 1);
                        parser->p = begin + parser->indent;
                        return bol(parser, location, value);
                }
        } else {
                size_t spaces = end - begin;
                if (parser->want == ITEMINDENT && spaces == parser->indent + 2) {
                        parser->bol = true;
                        parser->want = ERROR;
                        parser->indent += 2;
                        locate(parser, location, parser->indent);
                        return token(parser, NULL, begin + parser->indent, ITEMINDENT);
                } else if (spaces > parser->indent) {
                        locate(parser, location, spaces + 1);
                        parser->location.last_column--;
                        return substring(parser, NULL, value, end, SPACE);
                } else if (spaces < parser->indent) {
                        locate(parser, location, spaces + 1);
                        parser->location.last_column--;
                        if (spaces % 2 != 0)
                                return substring(parser, NULL, value, end, SPACE);
                        parser->bol = true;
                        return dedents(parser, begin, spaces);
                } else {
                       parser->p = end;
                       return bol(parser, location, value);
                }
        }
}

#define U_SINGLE_LEFT_QUOTATION_MARK ((uchar)0x2018)
#define U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x2039)
#define U_SUPERSCRIPT_PLUS_SIGN ((uchar)0x207a)

static inline bool
is_inline_symbol(const char *end)
{
        switch (u_dref(end)) {
        case '|':
        case '/':
        case '{':
        case U_SINGLE_LEFT_QUOTATION_MARK:
        case U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK:
        // NOTE This isn’t matched, as it may only appear after a superscript.
        // case U_SUPERSCRIPT_PLUS_SIGN:
                return true;
        default:
                return false;
        }
}

static inline bool
is_word_end(const char *string, const char *end)
{
        uchar c;
        size_t l;
        return is_space_or_end(end) ||
                (is_inline_symbol(end) &&
                 !u_isafteraletterornumeric(string, end)) ||
                (((c = u_lref(end, &l)) == '}' ||
                  (is_superscript(c) && (l += superscript(string + l), true))) &&
                 !uc_isaletterornumeric(u_dref(end + l)));
}

static inline int
word(struct parser *parser, YYLTYPE *location, YYSTYPE *value, const char *end)
{
        while (!is_word_end(parser->p, end))
                end++;
        if (end == parser->p)
                return token(parser, location, parser->p, END);
        return substring(parser, location, value, end, WORD);
}

#define U_SINGLE_RIGHT_QUOTATION_MARK ((uchar)0x2019)

static int
quoted(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *end = parser->p + 3;
        const char *nend;
        if (is_end(end) ||
            (u_dref(nend = u_next(end)) != U_SINGLE_RIGHT_QUOTATION_MARK))
                return word(parser, location, value, end);
        return substring(parser, location, value, nend, WORD);
}

#define U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x203a)

static int
code(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 3;
        const char *end = begin;
        size_t compact = 0;
        size_t length;
again:
        while (!is_end(end) &&
               !(u_lref(end, &length) == U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK &&
                 end - begin > 0))
                end += length;
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "expected ‘›’ after code inline (‹…›) content")) {
                        value->node = NULL;
                        goto oom;
                }
        } else {
                uchar c;
                do {
                        end += length;
                } while ((c = u_dref(end)) ==
                         U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK);
                if (c == U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK) {
                        if (compact == 0)
                                // NOTE left and right are the same number of bytes
                                compact = end - length - begin;
                        end += length;
                        goto again;
                }
                send = end - length;
        }
        value->node = text_node_new_dup(NMC_NODE_CODE, begin, send - begin);
        if (compact > 0) {
                char *p = ((struct nmc_text_node *)value->node)->text + compact;
                char *q = p + 3 * 2;
                while (*q != '\0') {
                        uchar c = u_lref(q, &length);
                        if (c == U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK) {
                                while ((c = u_lref(q + length, &length)) ==
                                       U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK) {
                                        for (size_t i = 0; i < length; i++)
                                                *p++ = *q++;
                                }
                                if (c == U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK) {
                                        // NOTE left and right are the same number of bytes
                                        q += 3 * 2;
                                        continue;
                                }
                        }
                        for (size_t i = 0; i < length; i++)
                                *p++ = *q++;
                }
                *p = '\0';
                char *t = realloc(((struct nmc_text_node *)value->node)->text,
                                  p - ((struct nmc_text_node *)value->node)->text + 1);
                if (t != NULL)
                        ((struct nmc_text_node *)value->node)->text = t;
        }
oom:
        return token(parser, location, end, CODE);
}

static int
emphasis(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 1;
        const char *end = begin;
        while (!is_end(end)) {
                if (*end == '/' && end - begin > 0) {
                        while (*(end + 1) == '/')
                                end++;
                        if (is_space_or_end(end + 1) ||
                            !uc_isaletterornumeric(u_dref(end + 1)))
                                break;
                }
                end++;
        }
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "expected ‘/’ after emphasized text (/…/)")) {
                        value->node = NULL;
                        goto oom;
                }
        } else
                end++;
        value->node = text_node_new_dup(NMC_NODE_EMPHASIS, begin, send - begin);
oom:
        return token(parser, location, end, EMPHASIS);
}

struct anchor_node {
        struct nmc_parent_node node;
        union {
                struct anchor *anchor;
                struct nmc_node_data *data;
        } u;
};

static struct nmc_node *
anchor_node_new(YYLTYPE *location, const char *string, size_t length)
{
        struct anchor_node *n = node_new(struct anchor_node,
                                         NMC_NODE_TYPE_PRIVATE,
                                         NMC_NODE_ANCHOR);
        if (n == NULL)
                return NULL;
        n->node.children = NULL;
        n->u.anchor = malloc(sizeof(struct anchor));
        if (n->u.anchor == NULL) {
                free(n);
                return NULL;
        }
        n->u.anchor->next = NULL;
        n->u.anchor->location = *location;
        char *id = mstrdup(string, length);
        if (id == NULL) {
                free(n->u.anchor);
                free(n);
                return NULL;
        }
        n->u.anchor->id = id_new(id);
        n->u.anchor->node = n;
        return (struct nmc_node *)n;
}

static int
parser_lex(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        if (parser->dedents > 0)
                return dedent(parser, location, parser->p);

        if (parser->bol)
                return bol(parser, location, value);

        size_t length;
        uchar c = u_lref(parser->p, &length);
        switch (c) {
        case ' ': {
                const char *end = parser->p;
                do {
                        end++;
                } while (*end == ' ');
                return substring(parser, location, value, end, SPACE);
        }
        case '\n':
                return eol(parser, location, value);
        case '|':
                return token(parser, location, parser->p + length, CELLSEPARATOR);
        case '/':
                return emphasis(parser, location, value);
        case '{':
                return token(parser, location, parser->p + length, BEGINGROUP);
        case '}':
                return token(parser, location, parser->p + length, ENDGROUP);
        case U_SINGLE_LEFT_QUOTATION_MARK:
                return quoted(parser, location, value);
        case U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK:
                return code(parser, location, value);
        case U_SUPERSCRIPT_PLUS_SIGN:
                return token(parser, location, parser->p + length, ANCHORSEPARATOR);
        }

        if (is_superscript(c)) {
                length += superscript(parser->p + length);
                const char *begin = parser->p;
                int r = token(parser, location, parser->p + length, ANCHOR);
                value->node = anchor_node_new(location, begin, length);
                return r;
        }

        return word(parser, location, value, parser->p);
}

static int
nmc_grammar_lex(YYSTYPE *value, YYLTYPE *location, struct parser *parser)
{
        int token;

        do {
                token = parser_lex(parser, location, value);
        } while (token == AGAIN);

        return token;
}

static void
nmc_grammar_error(YYLTYPE *location, struct parser *parser, const char *message)
{
        parser_error(parser, location, "%s", message);
}

static inline struct nodes
nodes(struct nmc_node *node)
{
        return (struct nodes){ node, node };
}

static inline struct nodes
sibling(struct nodes siblings, struct nmc_node *sibling)
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

static inline struct nmc_node *
parent1(enum nmc_node_name name, struct nmc_node *children)
{
        if (children == NULL)
                return NULL;
        struct nmc_parent_node *n = node_new(struct nmc_parent_node,
                                             NMC_NODE_TYPE_PARENT, name);
        if (n == NULL)
                return NULL;
        n->children = children;
        return (struct nmc_node *)n;
}

static inline struct nmc_node *
parent(enum nmc_node_name name, struct nodes children)
{
        return parent1(name, children.first);
}

static inline struct nmc_node *
parent_children(enum nmc_node_name name, struct nmc_node *first, struct nodes rest)
{
        first->next = rest.first;
        return parent1(name, first);
}

static void
clear_anchors(struct parser *parser)
{
        struct nmc_parser_error *first = NULL, *previous = NULL, *last = NULL;
        list_for_each_safe(struct anchor, p, n, parser->anchors) {
                first = nmc_parser_error_new(&p->location,
                                             "reference to undefined footnote: %s",
                                             p->id.string);
                if (first == NULL) {
                        nmc_parser_error_free(previous);
                        parser_oom(parser);
                        return;
                }
                if (last == NULL)
                        last = first;
                if (previous != NULL)
                        first->next = previous;
                previous = first;
        }
        if (first != NULL)
                parser_errors(parser, first, last);
        parser->anchors = NULL;
}

static bool
update_anchors(struct parser *parser, struct footnote *footnote)
{
        bool found = false;
        struct anchor *p = NULL;
        list_for_each_safe(struct anchor, c, n, parser->anchors) {
                if (id_eq(&c->id, &footnote->id)) {
                        if (footnote->node != NULL) {
                                c->node->node.node.type = footnote->node->node.node.type;
                                c->node->node.node.name = footnote->node->node.node.name;
                                c->node->u.data = footnote->node->data;
                                c->node->u.data->references++;
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
        return found || parser_error(parser, &footnote->location,
                                     "unreferenced footnote: %s",
                                     footnote->id.string);
}

static bool
reference(struct parser *parser, struct footnote **footnotes)
{
        list_for_each_safe(struct footnote, p, n, *footnotes) {
                if (!update_anchors(parser, p)) {
                        *footnotes = p;
                        return false;
                }
                footnote_free1(p);
        }
        return true;
}

static inline struct footnote *
fibling(struct parser *parser, struct footnote *footnotes, struct footnote *footnote)
{
        if (footnote == NULL)
                return NULL;
        struct footnote *last;
        list_for_each(struct footnote, p, footnotes) {
                if (id_eq(&footnote->id, &p->id)) {
                        char *s = nmc_location_str(&p->location);
                        if (s == NULL)
                                return NULL;
                        if (!parser_error(parser, &footnote->location,
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

static struct nmc_node *
definition(struct nmc_node *term, struct nmc_node *item)
{
        if (term == NULL)
                return NULL;
        term->next = parent1(NMC_NODE_DEFINITION, nmc_node_children(item));
        if (term->next == NULL)
                return NULL;
        nmc_node_children(item) = term;
        return item;
}

static inline struct nmc_node *
anchor(struct parser *parser, struct nmc_node *atom, struct nmc_node *anchor)
{
        if (anchor == NULL)
                return NULL;
        nmc_node_children(anchor) = atom;
        ((struct anchor_node *)anchor)->u.anchor->next = parser->anchors;
        parser->anchors = ((struct anchor_node *)anchor)->u.anchor;
        return anchor;
}

static inline struct nmc_node *
wanchor(struct parser *parser, struct substring substring, struct nmc_node *a)
{
        if (a == NULL)
                return NULL;
        struct nmc_node *n = text_node_new_dup(NMC_NODE_TEXT, substring.string, substring.length);
        if (n == NULL)
                return NULL;
        struct nmc_node *r = anchor(parser, n, a);
        if (r == NULL) {
                nmc_node_free(n);
                return NULL;
        }
        return r;
}

struct buffer_node {
        struct nmc_node node;
        union {
                char *text;
                struct buffer *buffer;
        } u;
};

static void
buffer_to_text(struct buffer_node *buffer)
{
        buffer->node.type = NMC_NODE_TYPE_TEXT;
        buffer->node.name = NMC_NODE_TEXT;
        buffer->u.text = buffer_str(buffer->u.buffer);
}

static struct nmc_node *
buffer(struct parser *parser, struct substring substring)
{
        struct buffer_node *n = node_new(struct buffer_node,
                                         NMC_NODE_TYPE_PRIVATE,
                                         NMC_NODE_BUFFER);
        if (n == NULL)
                return NULL;
        if (parser->buffer_node != NULL)
                buffer_to_text(parser->buffer_node);
        n->u.buffer = &parser->buffer;
        *n->u.buffer = (struct buffer)BUFFER_INIT;
        if (!buffer_append(n->u.buffer, substring.string, substring.length)) {
                free(n);
                parser->buffer_node = NULL;
                return NULL;
        }
        parser->buffer_node = n;
        return (struct nmc_node *)n;
}

static inline struct nodes
append_text(struct parser *parser, struct nodes inlines, struct substring substring)
{
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name != NMC_NODE_BUFFER)
                return sibling(inlines, buffer(parser, substring));
        if (!buffer_append(((struct buffer_node *)inlines.last)->u.buffer,
                           substring.string, substring.length))
                return nodes(NULL);
        return inlines;
}

static inline struct nodes
textify(struct parser *parser, struct nodes inlines)
{
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name == NMC_NODE_BUFFER) {
                buffer_to_text((struct buffer_node *)inlines.last);
                parser->buffer_node = NULL;
        }
        return inlines;
}

#define M(n) do { if ((n) == NULL) { parser_oom(parser); YYABORT; } } while (0)
#define N(n) M((n).last)
}
%%

nmc: ospace documenttitle oblockssections0 {
        M(parser->doc = parent_children(NMC_NODE_DOCUMENT, $2, $3));
        clear_anchors(parser);
};

documenttitle: words { M($$ = parent1(NMC_NODE_TITLE, textify(parser, $1).first)); };

words: WORD { N($$ = nodes(buffer(parser, $1))); }
| words WORD { N($$ = append_text(parser, $1, $2)); }
| words SPACE WORD { N($$ = append_text(parser, append_text(parser, $1, $2), $3)); };

oblockssections0: /* empty */ { $$ = nodes(NULL); }
| blockssections0 { $$ = $1; };

blockssections0: blocks
| blocks { clear_anchors(parser); } sections0 { $$ = siblings($1, $3); }
| sections0;

sections0: footnotedsection { clear_anchors(parser); $$ = nodes($1); }
| sections0 { clear_anchors(parser); } footnotedsection { $$ = sibling($1, $3); };

/*footnotedsection0: section0
| section0 footnotes { M($$ = reference(parser, &$2) ? $1 : NULL); };

section0: SECTION { parser->want = INDENT; } title oblockssections { M($$ = parent_children(NMC_NODE_SECTION, $3, $4)); };
*/

blockssections: blocks
| blocks sections { $$ = siblings($1, $2); }
| sections;

blocks: block { $$ = nodes($1); }
| blocks block { $$ = sibling($1, $2); }
| blocks footnotes %prec NotFootnote { N($$ = reference(parser, &$2) ? $1 : nodes(NULL)); };

block: PARAGRAPH inlines { M($$ = parent(NMC_NODE_PARAGRAPH, $2)); }
| itemizationitems %prec NotBlock { M($$ = parent(NMC_NODE_ITEMIZATION, $1)); }
| enumerationitems %prec NotBlock { M($$ = parent(NMC_NODE_ENUMERATION, $1)); }
| definitionitems %prec NotBlock { M($$ = parent(NMC_NODE_DEFINITIONS, $1)); }
| quotecontent { M($$ = parent(NMC_NODE_QUOTE, $1)); }
| CODEBLOCK { M($$ = $1); }
| headbody { M($$ = parent(NMC_NODE_TABLE, $1)); }
| figure { M($$ = parent(NMC_NODE_FIGURE, $1)); };

sections: footnotedsection { $$ = nodes($1); }
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes { M($$ = reference(parser, &$2) ? $1 : NULL); };

section: SECTION { parser->want = INDENT; } title oblockssections { M($$ = parent_children(NMC_NODE_SECTION, $3, $4)); };

title: inlines { M($$ = parent(NMC_NODE_TITLE, $1)); };

oblockssections: /* empty */ { $$ = nodes(NULL); }
| INDENT blockssections DEDENT { $$ = $2; };

footnotes: FOOTNOTE { M($$ = $1); }
| footnotes FOOTNOTE { M($$ = fibling(parser, $1, $2)); };

itemizationitems: itemizationitem { $$ = nodes($1); }
| itemizationitems itemizationitem { $$ = sibling($1, $2); };

itemizationitem: ITEMIZATION item { $$ = $2; };

enumerationitems: enumerationitem { $$ = nodes($1); }
| enumerationitems enumerationitem { $$ = sibling($1, $2); };

enumerationitem: ENUMERATION item { $$ = $2; };

definitionitems: definition { $$ = nodes($1); }
| definitionitems definition { $$ = sibling($1, $2); };

definition: TERM item { M($$ = definition($1, $2)); };

quotecontent: lines %prec NotBlock
| lines attribution { $$ = sibling($1, $2); };

lines: line { $$ = nodes($1); }
| lines line { $$ = sibling($1, $2); };

line: QUOTE inlines { M($$ = parent(NMC_NODE_LINE, $2)); };

attribution: ATTRIBUTION inlines { M($$ = parent(NMC_NODE_ATTRIBUTION, $2)); };

headbody: head body { $$ = sibling(nodes($1), $2); }
| body { $$ = nodes($1); };

head: row TABLESEPARATOR { M($$ = parent1(NMC_NODE_HEAD, $1)); };

body: rows %prec NotBlock { M($$ = parent(NMC_NODE_BODY, $1)); };

rows: row { $$ = nodes($1); }
| rows row { $$ = sibling($1, $2); };

row: ROW cells CELLSEPARATOR { M($$ = parent(NMC_NODE_ROW, $2)); };

cells: cell { $$ = nodes($1); }
| cells CELLSEPARATOR cell { $$ = sibling($1, $3); };

cell: inlines { M($$ = parent(NMC_NODE_CELL, $1)); };

figure: FIGURE title { N($$ = sibling(nodes($2), $1)); };

inlines: ospace sinlines ospace { $$ = textify(parser, $2); };

sinlines: WORD { N($$ = nodes(buffer(parser, $1))); }
| oanchoredinline { $$ = nodes($1); }
| sinlines WORD { N($$ = append_text(parser, $1, $2)); }
| sinlines oanchoredinline { N($$ = sibling(textify(parser, $1), $2)); }
| sinlines SPACE WORD { N($$ = append_text(parser, append_text(parser, $1, $2), $3)); }
| sinlines SPACE oanchoredinline { N($$ = sibling(textify(parser, append_text(parser, $1, $2)), $3)); };

oanchoredinline: inline
| anchoredinline;

anchoredinline: inline ANCHOR { M($$ = anchor(parser, $1, $2)); }
| WORD ANCHOR { M($$ = wanchor(parser, $1, $2)); }
| anchoredinline ANCHORSEPARATOR ANCHOR { M($$ = anchor(parser, $1, $3)); }

inline: CODE { M($$ = $1); }
| EMPHASIS { M($$ = $1); }
| BEGINGROUP sinlines ENDGROUP { M($$ = parent(NMC_NODE_GROUP, textify(parser, $2))); };

ospace: /* empty */
| SPACE;

item: { parser->want = ITEMINDENT; } firstparagraph oblocks { M($$ = parent_children(NMC_NODE_ITEM, $2, $3)); };

firstparagraph: inlines { M($$ = parent(NMC_NODE_PARAGRAPH, $1)); };

oblocks: /* empty */ { $$ = nodes(NULL); }
| ITEMINDENT blocks DEDENT { $$ = $2; };

%%

static YYSIZE_T
token_name_unescape(char *result, const char *escaped)
{
        if (*escaped != '"')
                return result == NULL ? yystrlen(escaped) :
                        (size_t)(yystpcpy(result, escaped) - result);
        YYSIZE_T n = 0;
        char const *p = escaped + 1;
        while (true) {
                char c;
                switch (*p) {
                case '\\': {
                        p++;
                        char *end;
                        long o = strtol(p, &end, 8);
                        if (end != p) {
                                c = o & 0xff;
                                p = end;
                        } else
                                c = *p++;
                        break;
                }
                case '"':
                        if (result != NULL)
                                result[n] = '\0';
                        return n;
                default:
                        c = *p++;
                        break;

                }
                if (result != NULL)
                        result[n] = c;
                n++;
        }
}

static char *
token_name(int type)
{
        const char *escaped = yytname[yytranslate[type]];
        char *r = malloc(strlen(escaped) + 1);
        if (r == NULL)
                return NULL;
        token_name_unescape(r, escaped);
        return r;
}

struct nmc_node *
nmc_parse(const char *input, struct nmc_parser_error **errors)
{
        struct parser parser;
        parser.p = input;
        parser.location = (YYLTYPE){ 1, 1, 1, 1 };
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.want = ERROR;
        parser.doc = NULL;
        parser.buffer_node = NULL;
        parser.anchors = NULL;
        parser.errors.first = parser.errors.last = NULL;

        nmc_grammar_parse(&parser);

        *errors = parser.errors.first;
        if (*errors != NULL) {
                nmc_node_free(parser.doc);
                return NULL;
        }
        return parser.doc;
}

bool
nmc_initialize(struct nmc_error *error)
{
        return definitions_init(error);
}

void
nmc_finalize(void)
{
        definitions_free();
}

char *
nmc_location_str(const struct nmc_location *l)
{
        char *s;
        if (l->first_line == l->last_line) {
                if (l->first_column == l->last_column)
                        asprintf(&s, "%d:%d", l->first_line, l->first_column);
                else
                        asprintf(&s, "%d.%d-%d", l->first_line, l->first_column, l->last_column);
        } else
                asprintf(&s, "%d.%d-%d.%d", l->first_line, l->first_column, l->last_line, l->last_column);
        return s;
}

static struct nmc_node *
parent_node_free(struct nmc_parent_node *node, UNUSED(struct parser *parser))
{
        return node->children;
}

static struct nmc_node *
data_node_free(struct nmc_data_node *node, struct parser *parser)
{
        node_data_free(node->data);
        return parent_node_free((struct nmc_parent_node *)node, parser);
}

static struct nmc_node *
text_node_free(struct nmc_text_node *node, UNUSED(struct parser *parser))
{
        free(node->text);
        return NULL;
}

static struct nmc_node *
private_node_free(struct nmc_node *node, struct parser *parser)
{
        switch (node->name) {
        case NMC_NODE_BUFFER:
                if (parser != NULL &&
                    parser->buffer_node == (struct buffer_node *)node)
                        parser->buffer_node = NULL;
                free(((struct buffer_node *)node)->u.buffer->content);
                return NULL;
        case NMC_NODE_ANCHOR: {
                if (parser != NULL) {
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
                anchor_free1(((struct anchor_node *)node)->u.anchor);
                return parent_node_free((struct nmc_parent_node *)node, parser);
        }
        default:
                assert(false);
        }
}

static void
nmc_node_unlink_and_free(struct nmc_node *node, struct parser *parser)
{
        typedef struct nmc_node *(*nodefreefn)(struct nmc_node *, struct parser *);
        static nodefreefn fns[] = {
                [NMC_NODE_TYPE_PARENT] = (nodefreefn)parent_node_free,
                [NMC_NODE_TYPE_DATA] = (nodefreefn)data_node_free,
                [NMC_NODE_TYPE_TEXT] = (nodefreefn)text_node_free,
                [NMC_NODE_TYPE_PRIVATE] = private_node_free,
        };

        if (node == NULL)
                return;
        struct nmc_node *p = node;
        struct nmc_node *last = p;
        while (last->next != NULL)
                last = last->next;
        while (p != NULL) {
                struct nmc_node *children = fns[p->type](p, parser);
                if (children != NULL) {
                        last->next = children;
                        while (last->next != NULL)
                                last = last->next;
                }
                struct nmc_node *next = p->next;
                free(p);
                p = next;
        }
}

void
nmc_node_free(struct nmc_node *node)
{
        nmc_node_unlink_and_free(node, NULL);
}
