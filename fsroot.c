/*
 * fsroot.c
 *
 *  Created on: 18 Nov 2016
 *      Author: Ander Juaristi
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "hash.h"
#include "mm.h"

/*
 * TODO
 *  - Implement fsroot_getattr()
 *  - Get the umask for the user calling us
 *  - Check that user & group exist
 *  - What happens when we have "../../..", etc?
 *  - Array & memory management utilities
 */
#define FSROOT_NOMORE		 1
#define FSROOT_OK		 0
#define FSROOT_E_BADARGS	-1
#define FSROOT_E_EXISTS		-2
#define FSROOT_E_NOTEXISTS	-3
#define FSROOT_E_NOMEM		-4
#define FSROOT_E_SYSCALL	-5

struct fsroot_file {
	mode_t mode;
	uid_t uid;
	gid_t gid;
};

/* TODO what does 'static' do here? */
static struct hash_table *files;

/*
 * Returns 1 if user and group *both* exist, 0 otherwise.
 * Optionally, returns the user's umask in the 'umask' field.
 */
static int fsroot_get_user(uid_t uid, gid_t gid, mode_t *umask)
{
	// TODO implement
	return 0;
}

static mode_t fsroot_umask()
{
	/* rw-r--r-- */
	return 0644;
}

int fsroot_getattr(const char *path, struct stat *out_st)
{
	struct stat st;
	struct fsroot_file *file;
	int retval = FSROOT_OK;

	if (!path || !out_st)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	if (stat(path, &st) == -1)
		return FSROOT_E_SYSCALL;

	st->st_mode = file->mode;
	st->st_uid = file->uid;
	st->st_gid = file->gid;
	memcpy(out_st, &st, sizeof(struct stat));

	return FSROOT_OK;
}

static struct fsroot_file *fsroot_create_file(uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_file *file = mm_new0(struct fsroot_file);

	file->uid = uid;
	file->gid = gid;
	file->mode = mode;

	return file;
}

int fsroot_symlink(const char *linkpath, const char *target, uid_t uid, gid_t gid, mode_t mode)
{
	char full_linkpath[PATH_MAX];
	int retval = FSROOT_OK;
	struct fsroot_file *file = NULL;

	if (!linkpath || !target ||
			!fsroot_fullpath(linkpath, full_linkpath, sizeof(full_linkpath)))
		return FSROOT_E_BADARGS;
	if (!S_ISLNK(mode)) /* This is not a symlink! */
		return FSROOT_E_BADARGS;

	if (hash_table_contains(files, linkpath))
		return FSROOT_E_EXISTS;

	if (symlink(target, full_linkpath) == -1)
		retval = FSROOT_E_SYSCALL;

	if (retval == FSROOT_OK) {
		/* symlink was created successfully, so we register it in our hash table */
		file = fsroot_create_file(uid, gid, mode);
		hash_table_put(files, linkpath, file);
	}

	return retval;
}

int fsroot_readlink(const char *linkpath, char *dst, size_t dstlen)
{
	char full_linkpath[PATH_MAX];
	struct fsroot_file *file;

	if (!linkpath || !dst || dstlen == 0 ||
			!fsroot_fullpath(linkpath, full_linkpath, sizeof(full_linkpath)))
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, linkpath);
	if (file == NULL || !S_ISLNK(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (readlink(full_linkpath, dst, dstlen) == -1)
		return FSROOT_E_SYSCALL;

	return FSROOT_OK;
}

int fsroot_mkdir(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
	char fullpath[PATH_MAX];
	struct fsroot_file *file;

	if (!path || !fsroot_fullpath(path, fullpath, sizeof(fullpath)))
		return FSROOT_E_BADARGS;
	if (!S_ISDIR(mode)) /* This is not a directory! */
		return FSROOT_E_BADARGS;

	if (hash_table_contains(files, path))
		return FSROOT_E_EXISTS;

	file = fsroot_create_file(uid, gid, mode);
	if (mkdir(fullpath, S_IFDIR & 0700) == -1)
		goto error;

	hash_table_put(files, path, file);

	return FSROOT_OK;
error:
	mm_free(file);
	return FSROOT_E_SYSCALL;
}

int fsroot_rmdir(const char *path)
{
	char fullpath[PATH_MAX];
	int retval = FSROOT_OK;
	struct fsroot_file *file;

	if (!path || !fsroot_fullpath(path, fullpath, sizeof(fullpath)))
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (file == NULL || !S_ISDIR(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (rmdir(fullpath) == -1)
		retval = FSROOT_E_SYSCALL;

	if (retval == FSROOT_OK) {
		hash_table_remove(files, path);
		mm_free(file);
	}

	return retval;
}

int fsroot_rename(const char *path, const char *newpath)
{
	struct fsroot_file *file;
	int retval = FSROOT_OK;
	char fullpath[PATH_MAX], full_newpath[PATH_MAX];

	if (!path || !newpath ||
			!fsroot_fullpath(path, fullpath, sizeof(fullpath)) ||
			!fsroot_fullpath(newpath, full_newpath, sizeof(full_newpath)))
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	if (hash_table_contains(files, newpath))
		return FSROOT_E_EXISTS;

	if (rename(fullpath, full_newpath) == -1)
		retval = FSROOT_E_SYSCALL;

	if (retval == FSROOT_OK) {
		hash_table_put(files, newpath, file);
		hash_table_remove(files, path);
	}

	return FSROOT_OK;
}

int fsroot_chmod(const char *path, mode_t mode)
{
	struct fsroot_file *file;
	mode_t filetype = mode & 0170000;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	/*
	 * Check that the user is not trying to change file type
	 * eg. directory to regular file
	 */
	if (filetype && ((file->mode & S_IFMT) != filetype))
		return FSROOT_E_BADARGS;

	file->mode = mode;
	return FSROOT_OK;
}

int fsroot_chown(const char *path, uid_t uid, gid_t gid)
{
	struct fsroot_file *file;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	file->uid = uid;
	file->gid = gid;
	return FSROOT_OK;
}

/*
 * We return a 'fsroot_file' to the user rather than a 'fsroot_directory', because
 * we do not want them to tinker with the directory's fields, such as num_entries.
 */
int fsroot_opendir(const char *path, void **outdir)
{
	struct fsroot_file *dir;
	int retval;
	DIR *dp;
	char fullpath[PATH_MAX];

	if (!path || !outdir ||
			!fsroot_fullpath(path, fullpath, sizeof(fullpath)))
		return FSROOT_E_BADARGS;

	dir = hash_table_get(files, path);
	if (dir && S_ISDIR(dir->mode)) {
		dp = opendir(fullpath);
		if (!dp) {
			retval = FSROOT_E_SYSCALL;
		} else {
			*outdir = dir;
			retval = FSROOT_OK;
		}
	} else {
		retval = FSROOT_E_NOTEXISTS;
	}

	return retval;
}

int fsroot_readdir(void *dir, char *out, size_t outlen)
{
	int initial_errno = errno;
	int retval = FSROOT_OK;
	struct dirent *de;

	if (!dir || !out || !outlen)
		return FSROOT_E_BADARGS;

__readdir:
	de = readdir((DIR *) dir);
	if (!de)
		goto end;

	if (!hash_table_contains(files, de->d_name))
		goto __readdir;
	if (strlen(de->d_name) > outlen)
		retval = FSROOT_E_NOMEM;
	if (retval == FSROOT_OK)
		strcpy(out, de->d_name);

	return retval;
end:
	/*
	 * If errno changed when we called readdir(),
	 * that means an error happened, actually.
	 * Else it just means there are no more files in the directory.
	 */
	return (initial_errno == errno ?
			FSROOT_NOMORE :
			FSROOT_E_SYSCALL);
}

void fsroot_closedir(void *dir)
{
	closedir((DIR *) dir);
}

void *fsroot_init(mode_t mask)
{
	mode_t umask = mask & 0777;
}
