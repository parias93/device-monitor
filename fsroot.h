/*
 * fsroot.h
 *
 *  Created on: 21 Oct 2016
 *      Author: Ander Juaristi
 */
#ifndef FSROOT_H_
#define FSROOT_H_
#include <linux/limits.h>
#include <sys/types.h>

#define FSROOT_NOMORE		 1
#define FSROOT_OK		 0
#define FSROOT_E_BADARGS	-1
#define FSROOT_E_EXISTS		-2
#define FSROOT_E_NOTEXISTS	-3
#define FSROOT_E_NOMEM		-4
#define FSROOT_E_SYSCALL	-5
#define FSROOT_EOF		-6
#define FSROOT_E_NOTOPEN	-7

int fsroot_init(const char *root);
void fsroot_deinit(void);

int fsroot_create(const char *path, uid_t uid, gid_t gid, mode_t mode, int flags, int *error_out);
int fsroot_open(const char *path, int flags);
int fsroot_read(int fd, char *buf, size_t size, off_t offset, int *error_out);
int fsroot_write(int fd, char *buf, size_t size, off_t offset, int *error_out);
int fsroot_sync(const char *path);
int fsroot_release(const char *path);

int fsroot_getattr(const char *path, struct stat *out_st);

int fsroot_symlink(const char *linkpath, const char *target, uid_t uid, gid_t gid, mode_t mode);
int fsroot_readlink(const char *linkpath, char *dst, size_t *dstlen);

int fsroot_mkdir(const char *path, uid_t uid, gid_t gid, mode_t mode);
int fsroot_rmdir(const char *path);

int fsroot_rename(const char *path, const char *newpath);
int fsroot_chmod(const char *path, mode_t mode);
int fsroot_chown(const char *path, uid_t uid, gid_t gid);

int fsroot_opendir(const char *path, void **outdir);
int fsroot_readdir(void *dir, char *out, size_t outlen);
void fsroot_closedir(void *dir);


#endif /* FSROOT_H_ */
