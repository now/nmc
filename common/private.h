#ifndef SRC_PRIVATE_H
#define SRC_PRIVATE_H

#ifndef __attribute__
#  if (! defined __GNUC__ || __GNUC__ < 2 \
       || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#    define __attribute__(Spec) /* empty */
#  endif
#endif

#ifndef PRINTF
#  define PRINTF(format_index, first_argument_index) \
        __attribute__((format(printf, format_index, first_argument_index)))
#endif

#ifndef PURE
#  define PURE __attribute__((pure))
#endif

#ifndef UNUSED
#  define UNUSED(v) v __attribute__((__unused__))
#endif

#define nmc_new(type) nmc_new_n(type, 1)

#define nmc_new_n(type, n) (type *)malloc(sizeof(type) * (n))

#define nmc_free(value) free(value)

#define nmc_lengthof(array) (sizeof(array) / sizeof((array)[0]))

#endif SRC_PRIVATE_H