TESTS = check_fsroot_1 check_fsroot_2

check_PROGRAMS = check_fsroot_1 check_fsroot_2

check_fsroot_1_SOURCES = check_fsroot_1.c $(top_builddir)/fsroot.h
check_fsroot_1_CFLAGS = @CHECK_CFLAGS@
check_fsroot_1_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@

check_fsroot_2_SOURCES = check_fsroot_2.c $(top_builddir)/fsroot.h
check_fsroot_2_CFLAGS = @CHECK_CFLAGS@
check_fsroot_2_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@
