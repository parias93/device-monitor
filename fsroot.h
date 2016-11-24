/*
 * fsroot.h
 *
 *  Created on: 21 Oct 2016
 *      Author: Ander Juaristi
 */
#ifndef FSROOT_H_
#define FSROOT_H_

#define FSROOT_MORE				 1
#define FSROOT_OK				 0
#define FSROOT_E_BADARGS			-1
#define FSROOT_E_EXISTS				-2
#define FSROOT_E_NOTEXISTS			-3
#define FSROOT_E_NOMEM				-4
#define FSROOT_E_NONEMPTY			-5
#define FSROOT_E_NEW_DIRECTORY_NOTEXISTS	-6

struct fsroot_file {
	char *name;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	void *priv;
};

int fsroot_symlink(const char *, const char *, uid_t, gid_t);
int fsroot_readlink(const char *, char *, size_t);
int fsroot_mkdir(const char *, uid_t, gid_t);
int fsroot_rmdir(const char *);
int fsroot_rename(const char *, const char *);
int fsroot_chmod(const char *, mode_t);
int fsroot_chown(const char *, uid_t, gid_t);
int fsroot_opendir(const char *, struct fsroot_file **);
int fsroot_readdir(off_t, struct fsroot_file *, struct fsroot_file *);

#endif /* FSROOT_H_ */
