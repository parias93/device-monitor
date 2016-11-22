/*
 * fsroot.h
 *
 *  Created on: 21 Oct 2016
 *      Author: Ander Juaristi
 */
#ifndef FSROOT_H_
#define FSROOT_H_
#include <linux/limits.h>

#define FSROOT_E_BADARGS	0x10000001 /* -1 */
#define FSROOT_E_BADFORMAT	0x10000010 /* -2 */
#define FSROOT_E_LIBC		0x10000011 /* -3 */

struct fsroot_file_perms {
	uid_t uid;
	gid_t gid;
	mode_t mode;
};

struct fsroot_file {
	struct fsroot_file_perms perms;
	char full_path[PATH_MAX];
};

int fsroot_fullpath(const char *in, char *out, size_t outlen);
int fsroot_get_file(const char *file_name, struct fsroot_file *file_out);
int fsroot_create_file(const char *file_name, struct fsroot_file_perms *perms);

#endif /* FSROOT_H_ */
