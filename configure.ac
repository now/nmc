AC_INIT([nmc], [1.0.0], [now@bitwi.se])
AC_CONFIG_SRCDIR([src/nmc.c])
AC_CONFIG_TESTDIR([test], [test])
AC_CONFIG_AUX_DIR([build])
AM_INIT_AUTOMAKE([foreign silent-rules subdir-objects -Wall -Werror])
AM_MISSING_PROG([AUTOM4TE], [autom4te])
AM_SILENT_RULES([yes])
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_RANLIB
AM_PROG_AR

AC_DEFUN([nmc_WARN_ADD],
[AS_VAR_PUSHDEF([nmc_Warn], [nmc_cv_warn_$1])dnl
AC_CACHE_CHECK([whether compiler handles $1], [nmc_Warn], [
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="${CPPFLAGS} $1"
  AC_PREPROC_IFELSE([AC_LANG_PROGRAM([])],
                    [AS_VAR_SET([nmc_Warn], [yes])],
                    [AS_VAR_SET([nmc_Warn], [no])])
  CPPFLAGS="$save_CPPFLAGS"
])
AS_VAR_PUSHDEF([nmc_Flags], m4_if([$2], [], [[WARN_CFLAGS]], [[$2]]))dnl
AS_VAR_IF([nmc_Warn], [yes], [AS_VAR_APPEND([nmc_Flags], [" $1"])])
AS_VAR_POPDEF([nmc_Flags])dnl
AS_VAR_POPDEF([nmc_Warn])dnl
m4_ifval([$2], [AS_LITERAL_IF([$2], [AC_SUBST([$2])], [])], [AC_SUBST([WARN_CFLAGS])])dnl
])
AC_ARG_ENABLE(gcc-warnings,
[  --enable-gcc-warnings   turn on lots of GCC warnings],
[case "${enableval}" in
    yes|no) ;;
    *)      AC_MSG_ERROR([bad value ${enableval} for gcc-warnings option]) ;;
 esac],
 [enableval=no])
if test "${enableval}" = yes; then
  nmc_WARN_ADD([-Werror], [WERROR_CFLAGS])
  nmc_WARN_ADD([-Wall])
  nmc_WARN_ADD([-Wcast-align])
  nmc_WARN_ADD([-Wextra])
  nmc_WARN_ADD([-Wformat=2])
  nmc_WARN_ADD([-Winit-self])
  nmc_WARN_ADD([-Winline])
  nmc_WARN_ADD([-Wmissing-declarations])
  nmc_WARN_ADD([-Wmissing-include-dirs])
  nmc_WARN_ADD([-Wmissing-noreturn])
  nmc_WARN_ADD([-Wmissing-prototypes])
  nmc_WARN_ADD([-Wnested-externs])
  nmc_WARN_ADD([-Wold-style-definition])
  nmc_WARN_ADD([-Wpacked])
  nmc_WARN_ADD([-Wp,-D_FORTIFY_SOURCE=2])
  nmc_WARN_ADD([-Wpointer-arith])
  nmc_WARN_ADD([-Wsign-compare])
  nmc_WARN_ADD([-Wstrict-aliasing=2])
  nmc_WARN_ADD([-Wsuggest-attribute=const])
  nmc_WARN_ADD([-Wsuggest-attribute=noreturn])
  nmc_WARN_ADD([-Wsuggest-attribute=pure])
  nmc_WARN_ADD([-Wundef])
  nmc_WARN_ADD([-Wwrite-strings])
fi

AC_PROG_YACC
AC_CHECK_PROGS([VALGRIND], [valgrind])
case $VALGRIND:$host_os in
  '':*) ;;
  *:darwin*) VALGRIND+=' --suppressions=$(abs_top_srcdir)/build/darwin.supp' ;;
esac

AC_CHECK_HEADERS([getopt.h])
AC_CHECK_FUNCS([getopt_long],,
  [AC_CHECK_LIB([gnugetopt],[getopt_long],[AC_DEFINE([HAVE_GETOPT_LONG])])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
  Makefile
  test/atlocal
])
AC_CONFIG_FILES([test/nmc], [chmod +x test/nmc])
AC_OUTPUT
