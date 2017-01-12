SUBDIRS = . tests

# Portable across other Autotools
# This must be consistent with AC_CONFIG_MACRO_DIR, in configure.ac
ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libfsroot.la
libfsroot_la_SOURCES = fsroot.c hash.c mm.c

bin_PROGRAMS = fsroot
fsroot_SOURCES = fsroot-main.c
fsroot_LDADD = libfsroot.la
fsroot_LDFLAGS = -pthread

