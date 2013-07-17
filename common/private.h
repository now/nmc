#ifndef __attribute__
#  if (! defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#    define __attribute__(x) /* empty */
#  endif
#endif

#ifndef NMC_PRINTF
#  define NMC_PRINTF(format_index, first_argument_index) \
        __attribute__((format(printf, format_index, first_argument_index)))
#endif

#ifndef NORETURN
#  define NORETURN __attribute__((noreturn))
#endif

#ifndef CONST
#  define CONST __attribute__((const))
#endif

#ifndef PURE
#  define PURE __attribute__((pure))
#endif

#ifndef UNUSED
#  define UNUSED(v) v __attribute__((__unused__))
#endif

#ifndef NON_NULL
#  define NON_NULL(parameters) __attribute__((__nonnull__ parameters))
#endif

#if defined(__GNUC__) && __GNUC__ > 2 && defined(__OPTIMIZE__)
#  define BOOLEAN_EXPR(expr) __extension__({ \
        int _boolean_var_; \
        if (expr) \
                _boolean_var_ = 1; \
        else \
                _boolean_var_ = 0; \
        _boolean_var_; \
})
#  define LIKELY(expr) (__builtin_expect(BOOLEAN_EXPR(expr), 1))
#  define UNLIKELY(expr) (__builtin_expect(BOOLEAN_EXPR(expr), 0))
#else
#  define LIKELY(expr) (expr)
#  define UNLIKELY(expr) (expr)
#endif

#define lengthof(array) (sizeof(array) / sizeof((array)[0]))

#ifndef HAVE_VASPRINTF
int vasprintf(char **output, const char *format, va_list args);
#endif

#ifndef HAVE_ASPRINTF
int asprintf(char **output, const char *format, ...) NMC_PRINTF(2, 3);
#endif
