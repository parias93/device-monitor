#include <check.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include "../fsroot.h"

char dir[] = "fsroot-root";

void check_symlink(const char *path, const char *target)
{
	int retval;
	struct stat st;
	char real_target[PATH_MAX];

	retval = lstat(path, &st);
	ck_assert_msg(retval == 0, "stat(\"%s\") returned %d\n", path, retval);
	ck_assert_msg(S_ISLNK(st.st_mode), "Path '%s' is not a symlink\n", path);
	ck_assert_int_eq(st.st_uid, 1000);
	ck_assert_int_eq(st.st_gid, 1000);

	retval = readlink(path, real_target, sizeof(real_target));
	ck_assert(retval != -1);
	real_target[retval] = 0;

	ck_assert_str_eq(target, real_target);
}

START_TEST(test_fsroot_symlink)
{
	int retval;
	char path[PATH_MAX];

	retval = fsroot_init(dir);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_init() returned %d\n", retval);

	retval = fsroot_symlink("foo", "foo-target", 1000, 1000, 0120600);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_symlink(\"foo\") returned %d\n", retval);
	ck_assert(snprintf(path, sizeof(path), "%s/foo", dir) > 0);
	check_symlink(path, "foo-target");

	memset(path, 0, sizeof(path));
	retval = fsroot_symlink("bar", "bar-target", 1000, 1000, 0120600);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_symlink(\"bar\") returned %d\n", retval);
	ck_assert(snprintf(path, sizeof(path), "%s/bar", dir) > 0);
	check_symlink(path, "bar-target");

	memset(path, 0, sizeof(path));
	retval = fsroot_symlink("foo2", "foo-target", 1000, 1000, 0120600);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_symlink(\"foo2\") returned %d\n", retval);
	ck_assert(snprintf(path, sizeof(path), "%s/foo2", dir) > 0);
	check_symlink(path, "foo-target");

	fsroot_deinit();
}
END_TEST

START_TEST(test_fsroot_readlink)
{
	int retval;
	char target[PATH_MAX];
	size_t len = sizeof(target);

	retval = fsroot_init(dir);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_init() returned %d\n", retval);

	retval = fsroot_symlink("a", "asdfasdf", 1000, 1000, 0120600);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_symlink(\"a\") returned %d\n", retval);

	retval = fsroot_readlink("a", target, &len);
	ck_assert_msg(retval == FSROOT_OK, "fsroot_readlink(\"a\") returned %d\n", retval);
	ck_assert_int_eq(len, sizeof(target));
	ck_assert_str_eq(target, "asdfasdf");

	fsroot_deinit();
}
END_TEST

void empty_dir()
{
	DIR *d;
	struct dirent *de = NULL;
	char path[PATH_MAX];

	d = opendir(dir);
	if (!d)
		ck_abort_msg("Could not open directory '%s'\n", dir);

	do {
		de = readdir(d);
		if (de && strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
			ck_assert(snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) > 0);
			ck_assert_msg(unlink(path) == 0,
					"Could not unlink file '%s'\n",
					de->d_name);
		}
	} while (de);

	closedir(d);
}

Suite *fsroot_suite()
{
	Suite *s;
	TCase *tc;

	s = suite_create("fsroot tests");

	tc = tcase_create("core");
	tcase_add_test(tc, test_fsroot_symlink);
	tcase_add_test(tc, test_fsroot_readlink);
	tcase_add_checked_fixture(tc, NULL, empty_dir);

	suite_add_tcase(s, tc);
	return s;
}

int main()
{
	int failed;
	Suite *s;
	SRunner *sr;

	if (mkdir(dir, 0744) == -1) {
		fprintf(stderr, "ERROR: Could not create directory '%s'\n", dir);
		return EXIT_FAILURE;
	}

	s = fsroot_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	if (rmdir(dir) == -1) {
		fprintf(stderr, "ERROR: Could not delete directory '%s'\n", dir);
		return EXIT_FAILURE;
	}

	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
