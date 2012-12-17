%{
#include <config.h>

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
#include "ext.h"
#include "unicode.h"

#define YYLTYPE struct nmc_location

struct substring {
        const char *string;
        size_t length;
};

struct nodes {
        struct node *first;
        struct node *last;
};

struct parser {
        const char *p;
        YYLTYPE location;
        size_t indent;
        size_t dedents;
        bool bol;
        int want;
        struct node *doc;
        struct anchor *anchors;
        struct {
                struct nmc_error *first;
                struct nmc_error *last;
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
strxdup(const char *source, size_t n)
{
        char *target = malloc(n + 1);
        if (target == NULL)
                return NULL;
        target[n] = '\0';
        return memcpy(target, source, n);
}

static void
anchor_unlink(struct node *node, struct parser *parser)
{
        if (node->name != NODE_ANCHOR)
                return;
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

static void
node_unlink_and_free(struct parser *parser, struct node *node)
{
        nmc_node_traverse(node, (nmc_node_traverse_fn)anchor_unlink,
                          nmc_node_traverse_null, parser);
        /* TODO Once nmc_node_traverse can actually handle reporting OOM, if
         * that occurs, parser->anchors must be completely cleared. */
        nmc_node_free(node);
}

struct footnote {
        struct footnote *next;
        YYLTYPE location;
        struct id id;
        struct auxiliary_node *node;
};

static void
footnote_free1(struct footnote *footnote)
{
        free(footnote->id.string);
        nmc_node_free((struct node *)footnote->node);
        free(footnote);
}

static void
footnote_free(struct footnote *footnote)
{
        list_for_each_safe(struct footnote, p, n, footnote)
                footnote_free1(p);
}
%}

%define api.pure
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
%token <node> TITLE
%token <substring> WORD
%token PARAGRAPH
%token SPACE
%token ITEMIZATION
%token ENUMERATION
%token <node> TERM
%token QUOTE
%token ATTRIBUTION
%token TABLESEPARATOR
%token ROW
%token ENTRYSEPARATOR "entry separator"
%token <node> CODEBLOCK
%token <footnote> FOOTNOTE
%token SECTION
%token INDENT
%token ITEMINDENT
%token DEDENT
%token <node> CODE
%token <node> EMPHASIS
%token <node> ANCHOR
%token ANCHORSEPARATOR
%token BEGINGROUP
%token ENDGROUP

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
%type <nodes> oblockssections0 blockssections blocks sections oblockssections
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
%type <node> head body row entry
%type <nodes> headbody rows entries
%type <nodes> inlines sinlines
%type <node> oanchoredinline anchoredinline inline
%type <node> item firstparagraph
%type <nodes> oblocks

%union {
        struct substring substring;
        struct nodes nodes;
        struct node *node;
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

%destructor { node_unlink_and_free(parser, $$.first); } <nodes>
%destructor { node_unlink_and_free(parser, $$); } <node>
%destructor { footnote_free($$); } <footnote>

%code
{
static inline bool
parser_is_oom(struct parser *parser)
{
        return parser->errors.last == &nmc_oom_error;
}

static void
parser_errors(struct parser *parser,
              struct nmc_error *first, struct nmc_error *last)
{
        if (parser_is_oom(parser)) {
                nmc_error_free(first);
                return;
        }
        if (parser->errors.first == NULL)
                parser->errors.first = first;
        if (parser->errors.last != NULL)
                parser->errors.last->next = first;
        parser->errors.last = last;
}

static void
parser_oom(struct parser *parser)
{
        if (parser_is_oom(parser))
                return;
        parser_errors(parser, &nmc_oom_error, &nmc_oom_error);
}

static bool NMC_PRINTF(3, 4)
parser_error(struct parser *parser, YYLTYPE *location,
             const char *message, ...)
{
        if (parser_is_oom(parser))
                return false;
        va_list args;
        va_start(args, message);
        struct nmc_error *error = nmc_error_newv(location, message, args);
        va_end(args);
        if (error == NULL) {
                parser_oom(parser);
                return false;
        }
        parser_errors(parser, error, error);
        return true;
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
        /* TODO: assert(parser->dedents > 0); */
        parser->dedents--;
        return token(parser, location, end, DEDENT);
}

static int
dedents(struct parser *parser, const char *begin, size_t spaces)
{
        parser->dedents = (parser->indent - spaces) / 2;
        parser->indent -= 2 * parser->dedents;
        /* TODO: assert(parser->indent >= 0); */
        return dedent(parser, NULL, begin + parser->indent);
}

typedef bool (*isfn)(uchar);

static inline size_t
length_of_run(struct parser *parser, isfn is)
{
        const char *end = parser->p;
        while (is(u_dref(end)))
                end = u_next(end);
        return end - parser->p;
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
superscript(struct parser *parser)
{
        return length_of_run(parser, is_superscript);
}

#define U_SUBSCRIPT_0 ((uchar)0x2080)
#define U_SUBSCRIPT_9 ((uchar)0x2089)

static inline bool
is_subscript(uchar c)
{
        return U_SUBSCRIPT_0 <= c && c <= U_SUBSCRIPT_9;
}

static inline size_t
subscript(struct parser *parser)
{
        return length_of_run(parser, is_subscript);
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
                        begin = end + parser->indent + 2;
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

        // NOTE We use a throwaway type here; caller must return actual type.
        token(parser, location, end, ERROR);
        return buffer_str(&b);
oom:
        token(parser, location, end, ERROR);
        free(b.content);
        return NULL;
}

static size_t
bol_space(struct parser *parser, size_t offset)
{
        if (*(parser->p + offset) == ' ')
                return 1;
        parser_error(parser, &parser->location,
                     "missing ‘ ’ after ‘%.*s’ at beginning of line",
                     (int)(u_next(parser->p) - parser->p), parser->p);
        return 0;
}

typedef struct auxiliary_node *(*definefn)(const char *, regmatch_t *);

struct definition {
        struct definition *next;
        regex_t regex;
        definefn define;
};

static struct definition *definitions;

#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static struct nmc_error *
nmc_regerror(YYLTYPE *location, int errcode, const regex_t *regex, const char *message)
{
        size_t l = regerror(errcode, regex, NULL, 0);
        char *s = malloc(l);
        if (s == NULL)
                return &nmc_oom_error;
        regerror(errcode, regex, s, l);
        struct nmc_error *e = nmc_error_new(location, message, s);
        free(s);
        return e == NULL ? &nmc_oom_error : e;
}
#pragma GCC diagnostic warning "-Wformat-nonliteral"

static bool
definitions_push(const char *pattern, definefn define, struct nmc_error **error)
{
        struct definition *definition = malloc(sizeof(struct definition));
        if (definition == NULL) {
                *error = &nmc_oom_error;
                return false;
        }
        int r = regcomp(&definition->regex, pattern, REG_EXTENDED);
        if (r != 0) {
                *error = nmc_regerror(&(YYLTYPE){ 0, 0, 0, 0 }, r,
                                      &definition->regex,
                                      "definition regex compilation failed: %s");
                free(definition);
                return false;
        }
        definition->define = define;
        definitions = list_cons(definition, definitions);
        return true;
}

static inline struct node *
node_init(struct node *node, enum node_type type, enum node_name name)
{
        if (node == NULL)
                return NULL;
        node->next = NULL;
        node->type = type;
        node->name = name;
        return node;
}
#define node_new(stype, type, name) ((stype *)node_init(malloc(sizeof(stype)), type, name))

static struct auxiliary_node *
auxiliary_node_new_matches(const char *name, const char *buffer,
                           regmatch_t *matches, int n, ...)
{
        struct auxiliary_node *d = node_new(struct auxiliary_node, AUXILIARY, NODE_AUXILIARY);
        if (d == NULL)
                return NULL;
        d->node.children = NULL;
        d->name = name;
        d->attributes = malloc(sizeof(struct auxiliary_node_attributes) +
                               sizeof(struct auxiliary_node_attribute) * (n + 1));
        if (d->attributes == NULL) {
                free(d);
                return NULL;
        }
        d->attributes->references = 1;
        regmatch_t *m = &matches[1];
        struct auxiliary_node_attribute *a = d->attributes->items;
        va_list args;
        va_start(args, n);
        for (int i = 0; i < n; i++) {
                /* TODO Check that rm_so/rm_eo ≠ -1 */
                a->name = va_arg(args, const char *);
                a->value = strxdup(buffer + m->rm_so, m->rm_eo - m->rm_so);
                if (a->value == NULL) {
                        va_end(args);
                        free(d->attributes);
                        free(d);
                        return NULL;
                }
                m++;
                a++;
        }
        va_end(args);
        a->name = NULL;
        a->value = NULL;
        return d;
}

static struct auxiliary_node *
abbreviation(const char *buffer, regmatch_t *matches)
{
        return auxiliary_node_new_matches("abbreviation", buffer, matches, 1, "for");
}

static struct auxiliary_node *
ref(const char *buffer, regmatch_t *matches)
{
        return auxiliary_node_new_matches("ref", buffer, matches, 2, "title", "uri");
}

static bool
definitions_init(struct nmc_error **error)
{
        if (definitions != NULL)
                return true;
        return  definitions_push("^(.+) +at +(.+)", ref, error) &&
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

static struct auxiliary_node *
define(YYLTYPE *location, const char *content, struct nmc_error **error)
{
        list_for_each(struct definition, p, definitions) {
                regmatch_t matches[p->regex.re_nsub + 1];
                int r = regexec(&p->regex, content,
                                p->regex.re_nsub + 1, matches, 0);
                if (r == 0)
                        return p->define(content, matches);
                else if (r != REG_NOMATCH) {
                        *error = nmc_regerror(location, r, &p->regex,
                                              "footnote definition regex execution failed: %s");
                        return NULL;
                }
        }

        *error = nmc_error_new(location, "unrecognized footnote content: %s", content);
        return NULL;
}

static int
footnote(struct parser *parser, YYLTYPE *location, YYSTYPE *value, size_t length)
{
        value->footnote = malloc(sizeof(struct footnote));
        if (value->footnote == NULL)
                return FOOTNOTE;
        value->footnote->next = NULL;
        char *id = strxdup(parser->p, length);
        if (id == NULL)
                goto oom;
        value->footnote->id = id_new(id);
        char *content = text(parser, location,
                             parser->p + length + bol_space(parser, length));
        if (content == NULL)
                goto oom_id;
        struct nmc_error *error = NULL;
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

static struct node *
text_node_new(enum node_name name, char *text)
{
        if (text == NULL)
                return NULL;
        struct text_node *n = node_new(struct text_node, TEXT, name);
        if (n == NULL) {
                free(text);
                return NULL;
        }
        n->text = text;
        return (struct node *)n;
}

static int
codeblock(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
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
                const char *sbegin = end + 1;
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
                if (!buffer_append_c(&b, '\n', lines))
                        goto oom;
                begin = sbegin + parser->indent + 4;
                end = send;
                parser->location.last_line += lines;
        }

        value->node = text_node_new(NODE_CODEBLOCK, buffer_str(&b));
        goto done;
oom:
        value->node = NULL;
done:
        return token(parser, location, end, CODEBLOCK);
}

static struct node *
text_node_new_dup(enum node_name name, const char *string, size_t length)
{
        return text_node_new(name, strxdup(string, length));
}

static int
term(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 1 + bol_space(parser, 1);
        const char *end = begin;

        while (!is_end(end)) {
                if (*end == '.') {
                        const char *send = end;
                        end++;
                        while (*end == ' ')
                                end++;
                        if (*end == '=' && is_space_or_end(end + 1)) {
                                value->node = text_node_new_dup(NODE_TERM, begin, send - begin);
                                return token(parser, location, end + 1, TERM);
                        }
                }
                end++;
        }

        int r = token(parser, location, end, AGAIN);
        parser_error(parser, location,
                     "missing ending “. =” for term in definition");
        return r;
}

static int
bol_token(struct parser *parser, YYLTYPE *location, size_t length, int type)
{
        return token(parser,
                     location,
                     parser->p + length + bol_space(parser, length),
                     type);
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
        case '/':
        case '>':
        case '|':
                return true;
        default:
                return is_subscript(c) || is_superscript(c);
        }
}

static int
bol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->bol = false;

        size_t length;
        if ((length = subscript(parser)) > 0)
                return bol_token(parser, location, length, ENUMERATION);
        else if ((length = superscript(parser)) > 0)
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
                return bol_token(parser, location, length, ITEMIZATION);
        case U_EM_DASH:
                return bol_token(parser, location, length, ATTRIBUTION);
        case '=':
                return term(parser, location, value);
        case '>':
                return bol_token(parser, location, length, QUOTE);
        case '|':
                if (*(parser->p + 1) == '-') {
                        // TODO This should probably be + 2.
                        const char *end = parser->p + 3;
                        while (!is_end(end))
                                end++;
                        return token(parser, location, end, TABLESEPARATOR);
                }
                return bol_token(parser, location, length, ROW);
        case '\0':
                return token(parser, location, parser->p, END);
        }

        if (c == U_BAD_INPUT_CHAR)
                parser_error(parser, &parser->location,
                             "broken UTF-8 sequence at beginning of line starting with %#02x",
                             *parser->p);
        else if (!uc_issolid(c))
                parser_error(parser, &parser->location,
                             "unrecognized character U+%04X at beginning of line",
                             c);
        else
                parser_error(parser, &parser->location,
                             "unrecognized character ‘%.*s’ (U+%04X) at beginning of line",
                             (int)length, parser->p, c);

        return PARAGRAPH;
}

static int
eol(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        parser->location.last_line++;
        parser->location.first_line = parser->location.last_line;
        parser->location.first_column = 1;

        const char *begin = parser->p + 1, *end = begin;
        while (*end == ' ')
                end++;
        if (*end == '\n') {
                parser->bol = true;
                end++;
                parser->location.last_line++;
                begin = end;
                while (true) {
                        while (*end == ' ')
                                end++;
                        if (*end != '\n')
                                break;
                        end++;
                        parser->location.last_line++;
                        begin = end;
                }
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
                        locate(parser, location, parser->indent + 1);
                        parser->location.last_column--;
                        return token(parser, NULL, begin + parser->indent, SPACE);
                } else if (spaces < parser->indent) {
                        locate(parser, location, spaces + 1);
                        parser->location.last_column--;
                        // TODO Complain about invalid dedent?
                        if (spaces % 2 != 0)
                                return token(parser, NULL, end, SPACE);
                        parser->bol = true;
                        return dedents(parser, begin, spaces);
                } else {
                       parser->p = end;
                       return bol(parser, location, value);
                }
        }
}

static inline bool
is_superscript_or_space_or_end(const char *end)
{
        return is_space_or_end(end) || is_superscript(u_dref(end));
}

static inline bool
is_group_end(const char *end)
{
        if (*end == '}') {
                do {
                        end++;
                } while (*end == '}');
                return is_superscript_or_space_or_end(end);
        }

        return false;
}

static inline bool
is_inline_end(const char *end)
{
        return is_superscript_or_space_or_end(end) ||
                is_group_end(end);
}

#define U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x203a)

static int
code(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 3;
        const char *end = begin;
        size_t length;
        while (!is_end(end) &&
               u_lref(end, &length) != U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                end += length;
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "missing ending ‘›’ for code inline")) {
                        value->node = NULL;
                        goto oom;
                }
        } else {
                while (u_dref(end) == U_SINGLE_RIGHT_POINTING_ANGLE_QUOTATION_MARK)
                        end += length;
                send = end - length;
        }
        value->node = text_node_new_dup(NODE_CODE, begin, send - begin);
oom:
        return token(parser, location, end, CODE);
}

static int
emphasis(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        const char *begin = parser->p + 1;
        const char *end = begin;
        while (!is_end(end) &&
               (*end != '/' || !is_inline_end(end + 1)))
                end++;
        const char *send = end;
        if (is_end(end)) {
                if (!parser_error(parser, &parser->location,
                                  "missing ending ‘/’ for emphasis inline")) {
                        value->node = NULL;
                        goto oom;
                }
        } else
                end++;
        value->node = text_node_new_dup(NODE_EMPHASIS, begin, send - begin);
oom:
        return token(parser, location, end, EMPHASIS);
}

struct anchor_node {
        struct parent_node node;
        union {
                struct anchor *anchor;
                struct {
                        const char *name;
                        struct auxiliary_node_attributes *attributes;
                } auxiliary;
        } u;
};

static struct node *
anchor_node_new(YYLTYPE *location, const char *string, size_t length)
{
        struct anchor_node *n = node_new(struct anchor_node, PRIVATE, NODE_ANCHOR);
        if (n == NULL)
                return NULL;
        n->u.anchor = malloc(sizeof(struct anchor));
        if (n->u.anchor == NULL) {
                free(n);
                return NULL;
        }
        n->u.anchor->next = NULL;
        n->u.anchor->location = *location;
        char *id = strxdup(string, length);
        if (id == NULL) {
                free(n->u.anchor);
                free(n);
                return NULL;
        }
        n->u.anchor->id = id_new(id);
        n->u.anchor->node = n;
        return (struct node *)n;
}

#define U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK ((uchar)0x2039)
#define U_SUPERSCRIPT_PLUS_SIGN ((uchar)0x207a)

static int
parser_lex(struct parser *parser, YYLTYPE *location, YYSTYPE *value)
{
        if (parser->dedents > 0)
                return dedent(parser, location, parser->p);

        if (parser->bol)
                return bol(parser, location, value);

        const char *end = parser->p;

        if (parser->want == TITLE) {
                parser->want = ERROR;
                value->node = text_node_new(NODE_TEXT, text(parser, location, end));
                return TITLE;
        }

        size_t length;
        uchar c = u_lref(end, &length);
        switch (c) {
        case ' ':
                do {
                        end++;
                } while (*end == ' ');
                return token(parser, location, end, SPACE);
        case '\n':
                return eol(parser, location, value);
        case '|':
                return token(parser, location, parser->p + length, ENTRYSEPARATOR);
        case '/':
                return emphasis(parser, location, value);
        case '{':
                return token(parser, location, parser->p + length, BEGINGROUP);
        case U_SINGLE_LEFT_POINTING_ANGLE_QUOTATION_MARK:
                return code(parser, location, value);
        case U_SUPERSCRIPT_PLUS_SIGN:
                // TODO Only catch this if followed by is_inline_end()
                // (or perhaps only if followed by superscript()).
                return token(parser, location, parser->p + length, ANCHORSEPARATOR);
        }

        if (is_group_end(end))
                return token(parser, location, parser->p + 1, ENDGROUP);
        else if ((length = superscript(parser)) > 0) {
                // TODO Only catch this if followed by is_inline_end().
                const char *begin = parser->p;
                int r = token(parser, location, parser->p + length, ANCHOR);
                value->node = anchor_node_new(location, begin, length);
                return r;
        }

        while (!is_inline_end(end))
                end++;
        if (end == parser->p)
                return token(parser, location, parser->p, END);

        return substring(parser, location, value, end, WORD);
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
nodes(struct node *node)
{
        return (struct nodes){ node, node };
}

static inline struct nodes
sibling(struct nodes siblings, struct node *sibling)
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

static inline struct node *
parent1(enum node_name name, struct node *children)
{
        if (children == NULL)
                return NULL;
        struct parent_node *n = node_new(struct parent_node, PARENT, name);
        if (n == NULL)
                return NULL;
        n->children = children;
        return (struct node *)n;
}

static inline struct node *
parent(enum node_name name, struct nodes children)
{
        return parent1(name, children.first);
}

static inline struct node *
parent_children(enum node_name name, struct node *first, struct nodes rest)
{
        first->next = rest.first;
        return parent1(name, first);
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

static void
anchor_free(struct anchor *anchor)
{
        list_for_each_safe(struct anchor, p, n, anchor)
                anchor_free1(p);
}

static void
report_remaining_anchors(struct parser *parser)
{
        struct nmc_error *first = NULL, *previous = NULL, *last = NULL;
        bool oom = false;
        list_for_each_safe(struct anchor, p, n, parser->anchors) {
                if (!oom) {
                        first = nmc_error_new(&p->location,
                                              "reference to undefined footnote: %s",
                                              p->id.string);
                        if (first == NULL)
                                oom = true;
                        else {
                                if (last == NULL)
                                        last = first;
                                if (previous != NULL)
                                        first->next = previous;
                                previous = first;
                        }
                }
                p->node->u.anchor = NULL;
                anchor_free1(p);
        }
        parser->anchors = NULL;
        if (oom) {
                nmc_error_free(previous);
                first = last = &nmc_oom_error;
        }
        parser_errors(parser, first, last);
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
                                c->node->u.auxiliary.name = footnote->node->name;
                                c->node->u.auxiliary.attributes = footnote->node->attributes;
                                footnote->node->attributes->references++;
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
        if (found)
                footnote->node = NULL;
        else if (!parser_error(parser,
                               &footnote->location,
                               "unreferenced footnote: %s",
                               footnote->id.string))
                return false;
        return true;
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

static struct node *
definition(struct node *term, struct node *item)
{
        if (term == NULL)
                return NULL;
        term->next = parent1(NODE_DEFINITION, ((struct parent_node *)item)->children);
        if (term->next == NULL)
                return NULL;
        ((struct parent_node *)item)->children = term;
        return item;
}

static inline struct node *
anchor(struct parser *parser, struct node *atom, struct node *anchor)
{
        if (anchor == NULL)
                return NULL;
        ((struct parent_node *)anchor)->children = atom;
        ((struct anchor_node *)anchor)->u.anchor->next = parser->anchors;
        parser->anchors = ((struct anchor_node *)anchor)->u.anchor;
        return anchor;
}

static inline struct node *
wanchor(struct parser *parser, struct substring substring, struct node *a)
{
        if (a == NULL)
                return NULL;
        struct node *n = text_node_new_dup(NODE_TEXT, substring.string, substring.length);
        if (n == NULL)
                return NULL;
        struct node *r = anchor(parser, n, a);
        if (r == NULL) {
                nmc_node_free(n);
                return NULL;
        }
        return r;
}

struct buffer_node {
        struct node node;
        union {
                char *text;
                struct buffer *buffer;
        } u;
};

static struct node *
buffer(struct substring substring)
{
        struct buffer_node *n = node_new(struct buffer_node, PRIVATE, NODE_BUFFER);
        if (n == NULL)
                return NULL;
        n->u.buffer = buffer_new(substring.string, substring.length);
        if (n->u.buffer == NULL) {
                free(n);
                return NULL;
        }
        return (struct node *)n;
}

static inline struct nodes
append_text(struct nodes inlines, struct substring substring)
{
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name != NODE_BUFFER)
                return sibling(inlines, buffer(substring));

        if (!buffer_append(((struct buffer_node *)inlines.last)->u.buffer,
                           substring.string, substring.length))
                return nodes(NULL);
        return inlines;
}

static inline struct nodes
append_space(struct nodes inlines)
{
        return append_text(inlines, (struct substring){ " ", 1 });
}

static inline struct nodes
textify(struct nodes inlines)
{
        if (inlines.last == NULL)
                return inlines;
        if (inlines.last->name == NODE_BUFFER) {
                struct buffer_node *buffer = (struct buffer_node *)inlines.last;
                buffer->node.type = TEXT;
                buffer->node.name = NODE_TEXT;
                struct buffer *b = buffer->u.buffer;
                buffer->u.text = buffer_str(b);
                // TODO Hack until we reuse the buffer
                b->content = NULL;
                buffer_free(b);
        }
        return inlines;
}

#define M(n) do { if ((n) == NULL) { parser_oom(parser); YYABORT; } } while (0)
#define N(n) M((n).last)
}

%%

nmc: documenttitle oblockssections0 {
        M(parser->doc = parent_children(NODE_DOCUMENT, $1, $2));
        report_remaining_anchors(parser);
};

documenttitle: TITLE { M($$ = parent1(NODE_TITLE, $1)); };

oblockssections0: /* empty */ { $$ = nodes(NULL); }
| blockssections { $$ = $1; };

blockssections: blocks
| blocks sections { $$ = siblings($1, $2); }
| sections;

blocks: block { $$ = nodes($1); }
| blocks block { $$ = sibling($1, $2); }
| blocks footnotes %prec NotFootnote { N($$ = reference(parser, &$2) ? $1 : nodes(NULL)); };

block: PARAGRAPH inlines { M($$ = parent(NODE_PARAGRAPH, $2)); }
| itemizationitems %prec NotBlock { M($$ = parent(NODE_ITEMIZATION, $1)); }
| enumerationitems %prec NotBlock { M($$ = parent(NODE_ENUMERATION, $1)); }
| definitionitems %prec NotBlock { M($$ = parent(NODE_DEFINITIONS, $1)); }
| quotecontent { M($$ = parent(NODE_QUOTE, $1)); }
| CODEBLOCK { M($$ = $1); }
| headbody { M($$ = parent(NODE_TABLE, $1)); };

sections: footnotedsection { $$ = nodes($1); }
| sections footnotedsection { $$ = sibling($1, $2); };

footnotedsection: section
| section footnotes { M($$ = reference(parser, &$2) ? $1 : NULL); };

section: SECTION { parser->want = INDENT; } title oblockssections { M($$ = parent_children(NODE_SECTION, $3, $4)); };

title: inlines { M($$ = parent(NODE_TITLE, $1)); };

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

line: QUOTE inlines { M($$ = parent(NODE_LINE, $2)); };

attribution: ATTRIBUTION inlines { M($$ = parent(NODE_ATTRIBUTION, $2)); };

headbody: head body { $$ = sibling(nodes($1), $2); }
| body { $$ = nodes($1); };

head: row TABLESEPARATOR { M($$ = parent1(NODE_HEAD, $1)); };

body: rows %prec NotBlock { M($$ = parent(NODE_BODY, $1)); };

rows: row { $$ = nodes($1); }
| rows row { $$ = sibling($1, $2); };

row: ROW entries ENTRYSEPARATOR { M($$ = parent(NODE_ROW, $2)); };

entries: entry { $$ = nodes($1); }
| entries ENTRYSEPARATOR entry { $$ = sibling($1, $3); };

entry: inlines { M($$ = parent(NODE_ENTRY, $1)); };

inlines: ospace sinlines ospace { $$ = textify($2); };

sinlines: WORD { N($$ = nodes(buffer($1))); }
| oanchoredinline { $$ = nodes($1); }
/* TODO $1.last can, if I see it correctly, never be NODE_BUFFER, so append_text may be unnecessary here. */
| sinlines WORD { N($$ = append_text($1, $2)); }
| sinlines oanchoredinline { N($$ = sibling(textify($1), $2)); }
| sinlines spaces WORD { N($$ = append_text(append_space($1), $3)); }
| sinlines spaces oanchoredinline { N($$ = sibling(textify(append_space($1)), $3)); };

oanchoredinline: inline
| anchoredinline;

anchoredinline: inline ANCHOR { M($$ = anchor(parser, $1, $2)); }
| WORD ANCHOR { M($$ = wanchor(parser, $1, $2)); }
| anchoredinline ANCHORSEPARATOR ANCHOR { M($$ = anchor(parser, $1, $3)); }

inline: CODE { M($$ = $1); }
| EMPHASIS { M($$ = $1); }
| BEGINGROUP sinlines ENDGROUP { M($$ = parent(NODE_GROUP, textify($2))); };

ospace: /* empty */
| SPACE;

spaces: SPACE
| spaces SPACE;

item: { parser->want = ITEMINDENT; } firstparagraph oblocks { M($$ = parent_children(NODE_ITEM, $2, $3)); };

firstparagraph: inlines { M($$ = parent(NODE_PARAGRAPH, $1)); }

oblocks: /* empty */ { $$ = nodes(NULL); }
| ITEMINDENT blocks DEDENT { $$ = $2; };

%%

struct node *
nmc_parse(const char *input, struct nmc_error **errors)
{
        struct parser parser;
        parser.p = input;
        parser.location = (YYLTYPE){ 1, 1, 1, 1 };
        parser.dedents = 0;
        parser.indent = 0;
        parser.bol = false;
        parser.want = TITLE;
        parser.doc = NULL;
        parser.anchors = NULL;
        parser.errors.first = parser.errors.last = NULL;

        nmc_grammar_parse(&parser);

        anchor_free(parser.anchors);

        *errors = parser.errors.first;
        return parser.doc;
}

bool
nmc_initialize(struct nmc_error **error)
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
                        nmc_asprintf(&s, "%d:%d", l->first_line, l->first_column);
                else
                        nmc_asprintf(&s, "%d.%d-%d", l->first_line, l->first_column, l->last_column);
        } else
                nmc_asprintf(&s, "%d.%d-%d.%d", l->first_line, l->first_column, l->last_line, l->last_column);
        return s;
}

static struct node *
parent_node_free(struct parent_node *node)
{
        return node->children;
}

static struct node *
auxiliary_node_free(struct auxiliary_node *node)
{
        if (--node->attributes->references == 0) {
                for (struct auxiliary_node_attribute *a = node->attributes->items; a->name != NULL; a++)
                        free(a->value);
                free(node->attributes);
        }
        return parent_node_free((struct parent_node *)node);
}

static struct node *
text_node_free(struct text_node *node)
{
        free(node->text);
        return NULL;
}

static struct node *
private_node_free(struct node *node)
{
        switch (node->name) {
        case NODE_BUFFER:
                buffer_free(((struct buffer_node *)node)->u.buffer);
                return NULL;
        case NODE_ANCHOR:
                anchor_free1(((struct anchor_node *)node)->u.anchor);
                return parent_node_free((struct parent_node *)node);
        default:
                /* TODO assert(false); */
                return NULL;
        }
}

void
nmc_node_free(struct node *node)
{
        typedef struct node *(*nodefreefn)(struct node *);
        static nodefreefn fns[] = {
                [PARENT] = (nodefreefn)parent_node_free,
                [AUXILIARY] = (nodefreefn)auxiliary_node_free,
                [TEXT] = (nodefreefn)text_node_free,
                [PRIVATE] = (nodefreefn)private_node_free,
        };

        if (node == NULL)
                return;
        struct node *p = node;
        struct node *last = p;
        while (last->next != NULL)
                last = last->next;
        while (p != NULL) {
                struct node *children = fns[p->type](p);
                if (children != NULL) {
                        last->next = children;
                        while (last->next != NULL)
                                last = last->next;
                }
                struct node *next = p->next;
                free(p);
                p = next;
        }
}
