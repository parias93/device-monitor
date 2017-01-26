TESTS = test_fsroot_create test_fsroot_symlink

check_PROGRAMS = test_fsroot_create test_fsroot_symlink

test_fsroot_create_SOURCES = test_fsroot_create.c $(top_builddir)/fsroot.h
test_fsroot_create_CFLAGS = @CHECK_CFLAGS@
test_fsroot_create_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@

test_fsroot_symlink_SOURCES = test_fsroot_symlink.c $(top_builddir)/fsroot.h
test_fsroot_symlink_CFLAGS = @CHECK_CFLAGS@
test_fsroot_symlink_LDADD = $(top_builddir)/libfsroot.la @CHECK_LIBS@
