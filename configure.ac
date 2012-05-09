AC_INIT([nmc], [1.0.0], [now@bitwi.se])
AC_CONFIG_SRCDIR([src/nmc.c])
AC_CONFIG_TESTDIR([test], [test])
AC_CONFIG_AUX_DIR([build])
AM_INIT_AUTOMAKE([foreign silent-rules subdir-objects -Wall -Werror])
AM_MISSING_PROG([AUTOM4TE], [autom4te])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
PKG_CHECK_MODULES([XML], [libxml-2.0])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
  Makefile
  test/atlocal
])
AC_CONFIG_FILES([test/nmc], [chmod +x test/nmc])
AC_OUTPUT
