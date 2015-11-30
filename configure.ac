AC_PREREQ([2.69])
AC_INIT([nmc], m4_esyscmd([build/git-version]), [now@bitwi.se])
PACKAGE_VERSION_MM=m4_bpatsubst(AC_PACKAGE_VERSION, [^\([0-9][0-9]*\.[0-9][0-9]*\).*$], [\1])
AC_SUBST([PACKAGE_VERSION_MM])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_SRCDIR([src/nmc.c])
AC_CONFIG_TESTDIR([test], [test])
AC_CONFIG_AUX_DIR([build])
AM_INIT_AUTOMAKE([1.13.1 foreign silent-rules subdir-objects -Wall -Werror])
AM_MISSING_PROG([AUTOM4TE], [autom4te])
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_RANLIB
AM_PROG_AR
AC_PATH_PROG([XMLCATALOG], [xmlcatalog])
AC_PATH_PROG([XSLTPROC], [xsltproc])

AC_ARG_ENABLE([gcc-warnings],
[  --enable-gcc-warnings   turn on lots of GCC warnings],
[case "${enableval}" in
    yes|no) ;;
    *)      AC_MSG_ERROR([bad value ${enableval} for gcc-warnings option]) ;;
 esac],
 [enableval=no])
if test "${enableval}" = yes; then
  gl_WARN_ADD([-pedantic])
  gl_WARN_ADD([-Werror], [WERROR_CFLAGS])
  gl_WARN_ADD([-Wall])
  gl_WARN_ADD([-Wcast-align])
  gl_WARN_ADD([-Wextra])
  gl_WARN_ADD([-Wformat=2])
  gl_WARN_ADD([-Winit-self])
  gl_WARN_ADD([-Winline])
  gl_WARN_ADD([-Wmissing-declarations])
  gl_WARN_ADD([-Wmissing-format-attribute])
  gl_WARN_ADD([-Wmissing-include-dirs])
  gl_WARN_ADD([-Wmissing-noreturn])
  gl_WARN_ADD([-Wmissing-prototypes])
  gl_WARN_ADD([-Wnested-externs])
  gl_WARN_ADD([-Wold-style-definition])
  gl_WARN_ADD([-D_FORTIFY_SOURCE=2])
  gl_WARN_ADD([-Wpacked])
  gl_WARN_ADD([-Wpointer-arith])
  gl_WARN_ADD([-Wshorten-64-to-32])
  gl_WARN_ADD([-Wsign-compare])
  gl_WARN_ADD([-Wstrict-aliasing=2])
  gl_WARN_ADD([-Wsuggest-attribute=const])
  gl_WARN_ADD([-Wsuggest-attribute=noreturn])
  gl_WARN_ADD([-Wsuggest-attribute=pure], [WSUGGESTATTRIBUTEPURE_CFLAGS])
  gl_WARN_ADD([-Wundef])
  gl_WARN_ADD([-Wwrite-strings])
  if test -n "$WSUGGESTATTRIBUTEPURE_CFLAGS"; then
    AC_DEFINE([HAVE_WSUGGESTATTRIBUTE_PURE], [1],
              [Define to 1 if compiler supports -Wsuggestattribute=pure.])
  fi
fi

AC_CANONICAL_HOST

AC_PROG_YACC
AC_CHECK_PROGS([VALGRIND], [valgrind])
case $VALGRIND:$host_os in
  '':*) ;;
  *:darwin*) VALGRIND+=' --suppressions=$(abs_top_srcdir)/build/darwin.supp' ;;
esac
AC_CHECK_PROGS([WGET], [wget])

AC_CHECK_HEADERS([getopt.h])
AC_CHECK_FUNCS([getopt_long],,
  [AC_CHECK_LIB([gnugetopt],[getopt_long],[AC_DEFINE([HAVE_GETOPT_LONG])])])
AC_REPLACE_FUNCS([asprintf vasprintf])

AC_ARG_ENABLE([xml-catalog-update],
[  --enable-xml-catalog-update
                           update system XML catalog during installation],
[case "${enableval}" in
    yes|no) ;;
    *)      AC_MSG_ERROR([bad value ${enableval} for xml-catalog-update option]) ;;
 esac],
 [enableval=yes])
AM_CONDITIONAL([XML_CATALOG_UPDATE], [test x$enableval = xyes])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
  Makefile
  test/atlocal
])
AC_CONFIG_FILES([test/nmc], [chmod +x test/nmc])
AC_OUTPUT
