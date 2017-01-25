TESTS = check_fsroot_1 check_fsroot_2

check_PROGRAMS = test_fsroot_create check_fsroot_2

test_fsroot_create_SOURCES = test_fsroot_create.c $(top_builddir)/fsroot.h
test_fsroot_create_CFLAGS = @CHECK_CFLAGS@
test_fsroot_create_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@

check_fsroot_2_SOURCES = check_fsroot_2.c $(top_builddir)/fsroot.h
check_fsroot_2_CFLAGS = @CHECK_CFLAGS@
check_fsroot_2_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@
