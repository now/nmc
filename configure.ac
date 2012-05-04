AC_INIT([nmc], [1.0.0], [now@bitwi.se])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_TESTDIR([test])
AC_CONFIG_AUX_DIR([build])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_MISSING_PROG([AUTOM4TE], [autom4te])
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.30])
AC_CONFIG_FILES([
  Makefile
  test/atlocal
])
AC_OUTPUT
