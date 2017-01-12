AC_INIT([libfsroot], [1.0], [ander.juaristi AT tecnalia.com])

AM_INIT_AUTOMAKE([gnu -Wall -Werror])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile tests/Makefile])
AC_CONFIG_MACRO_DIR([m4])

PKG_CHECK_MODULES([CHECK], [check >= 0.9.6])

AC_PROG_CC
# This is needed for the '-std=gnu99' flag, which is also needed
AC_PROG_CC_STDC
AM_PROG_AR
# Turn on processing for libtool
AC_PROG_LIBTOOL

LT_INIT

AC_OUTPUT
