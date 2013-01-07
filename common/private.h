#ifndef __attribute__
#  if (! defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#    define __attribute__(x) /* empty */
#  endif
#endif

#define NMC_PRINTF(format_index, first_argument_index) \
        __attribute__((format(printf, format_index, first_argument_index)))

#ifndef NORETURN
#  define NORETURN __attribute__((noreturn))
#endif

#ifndef PURE
#  define PURE __attribute__((pure))
#endif

#ifndef UNUSED
#  define UNUSED(v) v __attribute__((__unused__))
#endif

#define lengthof(array) (sizeof(array) / sizeof((array)[0]))

#ifndef HAVE_VASPRINTF
int vasprintf(char **output, const char *format, va_list args);
#endif

#ifndef HAVE_ASPRINTF
int asprintf(char **output, const char *format, ...) NMC_PRINTF(2, 3);
#endif
