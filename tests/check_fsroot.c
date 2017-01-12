#include <stdlib.h>
#include <check.h>
#include "../fsroot.h"

START_TEST(test_one)
{
	ck_abort_msg("This test intentionally aborted");
}
END_TEST

START_TEST(test_two)
{
	ck_assert_msg(1, "This test intentionally passed");
}
END_TEST

Suite *fsroot_suite()
{
	Suite *s;
	TCase *tc;

	s = suite_create("fsroot tests");

	tc = tcase_create("core");
	tcase_add_test(tc, test_one);
	tcase_add_test(tc, test_two);

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

	return (failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
