#include <check.h>
#include <fcntl.h>
#include <stdlib.h>
#include "../fsroot.h"

char foo_data[] = "Hello, world! This is 'foo'.",
	bar_data[] = "Hi, I'm 'bar'!";

START_TEST(test_fsroot_create)
{
	int retval, err = 0;
	int fd_foo, fd_bar;

	retval = fsroot_init("fsroot-root");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_init() returned %d\n", retval);

	fd_foo = fsroot_create("foo", 1000, 1000, 0100700, O_CREAT | O_RDWR, &err);
	ck_assert_msg(fd_foo >= 0, "fsroot_create(\"foo\") returned %d (err: %d)\n", fd_foo, err);
	fd_bar = fsroot_create("bar", 1000, 1000, 0100700, O_RDWR, &err);
	ck_assert_msg(fd_bar >= 0 && fd_bar != fd_foo, "fsroot_create(\"bar\") returned %d (err: %d)\n",
			fd_bar, err);

	retval = fsroot_write(fd_foo, foo_data, sizeof(foo_data) - 1, 0, &err);
	ck_assert_msg(retval == (sizeof(foo_data) - 1), "fsroot_write(\"foo\") returned %d (err: %d)\n",
			retval, err);
	retval = fsroot_write(fd_bar, bar_data, sizeof(bar_data) - 1, 0, &err);
	ck_assert_msg(retval == (sizeof(bar_data) - 1), "fsroot_write(\"bar\") returned %d (err: %d)\n",
			retval, err);

	retval = fsroot_release("foo");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_release(\"foo\") returned %d\n", retval);
	retval = fsroot_release("bar");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_release(\"bar\") returned %d\n", retval);

	fsroot_deinit();
}
END_TEST

START_TEST(test_fsroot_open)
{
	int retval, err = 0;
	int fd_foo, fd_bar;
	char buf[500];
	size_t bufsize = sizeof(buf);

	retval = fsroot_init("fsroot-root");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_init() returned %d\n", retval);

	fd_foo = fsroot_open("foo", O_CREAT | O_RDONLY);
	ck_assert_msg(fd_foo >= 0, "fsroot_open(\"foo\") returned %d\n",
			fd_foo);
	fd_bar = fsroot_open("bar", 0);
	ck_assert_msg(fd_bar >= 0 && fd_bar != fd_foo, "fsroot_open(\"bar\") returned %d\n",
			fd_bar);

	retval = fsroot_read(fd_foo, buf, bufsize, 0, &err);
	ck_assert_msg(retval != strlen(foo_data), "fsroot_read(\"foo\") returned %d (err: %d)\n",
			retval, err);
	ck_assert_str_eq(buf, foo_data);
	retval = fsroot_read(fd_bar, buf, bufsize, 0, &err);
	ck_assert_msg(retval != strlen(bar_data), "fsroot_read(\"bar\") returned %d (err: %d)\n",
			retval, err);
	ck_assert_str_eq(buf, bar_data);

	retval = fsroot_release("foo");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_release(\"foo\") returned %d\n", retval);
	retval = fsroot_release("bar");
	ck_assert_msg(retval == FSROOT_OK, "fsroot_release(\"bar\") returned %d\n", retval);

	fsroot_deinit();
}
END_TEST

Suite *fsroot_suite()
{
	Suite *s;
	TCase *tc;

	s = suite_create("fsroot tests");

	tc = tcase_create("core");
	tcase_add_test(tc, test_fsroot_create);

	suite_add_tcase(s, tc);
	return s;
}

int main()
{
	int failed;
	Suite *s;
	SRunner *sr;

	s = fsroot_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
