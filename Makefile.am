SUBDIRS = . tests

# Portable across other Autotools
# This must be consistent with AC_CONFIG_MACRO_DIR, in configure.ac
ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libfsroot.la
libfsroot_la_SOURCES = fsroot.c hash.c mm.c
libfsroot_la_LDFLAGS = -pthread

