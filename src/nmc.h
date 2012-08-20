#ifndef SRC_NMC_H
#define SRC_NMC_H

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

#ifndef UNUSED
#  define UNUSED(v) v __attribute__((__unused__))
#endif

#endif
