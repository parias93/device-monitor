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
#include <pthread.h>
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
#define FSROOT_EOF		-6
#define FSROOT_E_NOTOPEN	-7

struct fsroot_file {
	const char *path;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	off_t cur_offset;
	struct {
		int append    : 1;
		int sync      : 1;
		int sync_read : 1;
		int tmpfile   : 1;
		int is_synced : 1;
	} flags;
	pthread_rwlock_t rwlock;
	char *buf;
	size_t buf_len;
};

struct fsroot_file_descriptor {
	int8_t can_read,
		can_write,
		deleted;
	struct fsroot_file *file;
};

static struct hash_table *files;
/*
 * TODO this should be initialized in fsroot_init()
 * with some sensible defaults
 */
static struct {
	struct fsroot_file_descriptor **file_descriptors;
	size_t num_files;
	size_t num_slots;
	pthread_rwlock_t rwlock;
} open_files;

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

static int fsroot_create_file_buffer(struct fsroot_file *file, int *error_out)
{
	off_t offset;
	FILE *fp = fopen(file->path, "r");
	if (fp == NULL)
		goto error;

	file->buf = NULL;
	file->buf_len = 0;

	if (feof(fp))
		/* File is empty. Return an empty buffer. */
		goto end;

	offset = fseek(fp, 0, SEEK_END);
	if (offset == -1)
		goto error;

	fseek(fp, 0, SEEK_SET);

	file->buf = mm_new0n(offset);
	file->buf_len = fread(file->buf, 1, offset, fp);
	if (file->buf_len == -1)
		goto error;
	/*
	 * For some reason, we happened to read fewer bytes than expected
	 * so resize the buffer
	 */
	if (file->buf_len < offset)
		file->buf = mm_realloc(file->buf, file->buf_len);
end:
	fclose(fp);

	return 0;

error:
	if (file->buf)
		xfree(file->buf);
	if (fp)
		fclose(fp);
	if (error_out)
		*error_out = errno;
	return -1;
}

/*
 * Returns a negative number on error,
 * or else the file descriptor.
 */
static struct fsroot_file *fsroot_create_file(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_file *file = mm_new0(struct fsroot_file);

	file->path = path;
	file->uid = uid;
	file->gid = gid;
	file->mode = mode;

	return file;
}

static int fsroot_sync_file(struct fsroot_file *file)
{
	int retval = FSROOT_OK, fd;

	/* If file has no buffer, it has already be synced */
	if (!file->buf)
		goto end;

	fd = open(file->path, O_WRONLY | O_EXCL);
	if (fd == -1) {
		retval = FSROOT_E_SYSCALL;
		goto end;
	}

	pthread_rwlock_rdlock(&file->rwlock);
	write(fd, file->buf, file->buf_len);
	pthread_rwlock_unlock(&file->rwlock);
	fsync(fd);
	close(fd);

end:
	return retval;
}

/*
 * TODO take a look at this in the disassembler:
 *
 * 	return open_files.num_files++;
 */
static size_t __fsroot_open(struct fsroot_file *file, int flags)
{
	struct fsroot_file_descriptor *fildes = mm_new0(struct fsroot_file_descriptor);
	size_t retval;

	if (flags & O_RDONLY == O_RDONLY) {
		fildes->can_read = 1;
	} else if (flags & O_WRONLY == O_WRONLY) {
		fildes->can_write = 1;
	} else if (flags & O_RDWR == O_RDWR) {
		fildes->can_read = 1;
		fildes->can_write = 1;
	}

	fildes->file = file;

	/*
	 * Finally, add the file descriptor to the array of open files,
	 * resizing the array if needed.
	 */
	pthread_rwlock_wrlock(&open_files.rwlock);
	if (open_files.num_files == open_files.num_slots) {
		open_files.num_slots <<= 1;
		/* FIXME: if mm_reallocn() fails, the lock remains held (handle this in the no-mem callback) */
		open_files.file_descriptors = mm_reallocn(open_files.file_descriptors,
				open_files.num_slots,
				sizeof(struct fsroot_file_descriptor *));
	}

	open_files.file_descriptors[open_files.num_files] = fildes;
	retval = open_files.num_files++;
	pthread_rwlock_unlock(&open_files.rwlock);

	return retval;
}

static unsigned int __fsroot_close(struct fsroot_file *file)
{
	struct fsroot_file_descriptor *fildes, **file_descriptors;
	unsigned int num_files = 0, num_deleted_files = 0;

	pthread_rwlock_wrlock(&open_files.rwlock);
	/*
	 * Walk through all the file descriptors
	 * and mark as deleted those that refer to this file.
	 */
	for (unsigned int i = 0; i < open_files.num_files; i++) {
		fildes = open_files.file_descriptors[i];
		if (fildes->file == file) {
			fildes->deleted = 1;
			num_deleted_files++;
		}
	}

	file_descriptors = mm_new(open_files.num_slots,
			struct fsroot_file_descriptor *);

	/*
	 * Copy all the non-deleted file descriptors to the new array,
	 * and free the deleted ones.
	 */
	for (unsigned int i = 0; i < open_files.num_files; i++) {
		fildes = open_files.file_descriptors[i];
		if (!fildes->deleted)
			file_descriptors[num_files++] = fildes;
		else
			xfree(fildes);
	}

	xfree(open_files.file_descriptors);
	open_files.file_descriptors = file_descriptors;
	open_files.num_files = num_files;
	pthread_rwlock_unlock(&open_files.rwlock);

	return num_deleted_files;
}

int fsroot_create(const char *path, uid_t uid, gid_t gid, mode_t mode, int flags, int *error_out)
{
	int error = 0, retval = FSROOT_OK, fd;
	struct fsroot_file *file;

	if (!path || !mode || !S_ISREG(mode))
		return FSROOT_E_BADARGS;
	if (hash_table_contains(files, path))
		return FSROOT_E_EXISTS;

	/* These flags are not valid, and we'll return an error if we find them */
	if (flags & O_ASYNC == O_ASYNC) {
		error = EINVAL;
		retval = FSROOT_E_SYSCALL;
	} else if (flags & O_DIRECTORY == O_DIRECTORY) {
		/*
		 * fsroot_create() is only used for regular files.
		 * Directories are opened with fsroot_opendir().
		 */
		error = ENOTDIR;
		retval = FSROOT_E_SYSCALL;
	} else if (flags & O_NOCTTY == O_NOCTTY) {
		error = EINVAL;
		retval = FSROOT_E_SYSCALL;
	} else if (flags & O_NOFOLLOW == O_NOFOLLOW) {
		/* FIXME: O_NOFOLLOW should be ignored if O_PATH is present */
		error = ELOOP;
		retval = FSROOT_E_SYSCALL;
	}

	if (retval != FSROOT_OK)
		goto end;

	file = fsroot_create_file(path, uid, gid, mode);
	/*
	 * These flags will be handled internally:
	 * 	- O_EXCL
	 * 	- O_APPEND
	 * 	- O_DSYNC
	 * 	- O_SYNC
	 * 	- O_TMPFILE
	 *
	 * Thus, if they're present in 'flags', we strip them out.
	 */
	if (flags & O_EXCL == O_EXCL) {
		/* We simply ignore O_EXCL, since we pass it to open(2) anyway */
		flags ^= O_EXCL;
	}
	if (flags & O_APPEND == O_APPEND) {
		flags ^= O_APPEND;
		file->flags.append = 1;
	}
#ifdef O_DSYNC
	if (flags & O_DSYNC == O_DSYNC) {
		flags ^= O_DSYNC;
		file->flags.sync = 1;
	}
#endif
#ifdef O_SYNC
	/* In Linux, O_SYNC is equivalent to O_RSYNC */
	if (flags & O_SYNC == O_SYNC) {
		flags ^= O_SYNC;
		file->flags.sync_read = 1;
	}
#endif
#ifdef O_TMPFILE
	if (flags & O_TMPFILE == O_TMPFILE) {
		flags ^= O_TMPFILE;
		file->flags.tmpfile = 1;
	}
#endif

	/* Now issue the system call */
	fd = open(path, O_CREAT | O_EXCL | flags);
	if (fd == -1) {
		retval = FSROOT_E_SYSCALL;
		goto end;
	}

	/*
	 * File was correctly created.
	 * We now register it in our hash table and close
	 * the real file.
	 */
	close(fd);
	hash_table_put(files, path, file);

end:
	/*
	 * Finally, if the file was correctly created,
	 * generate a file descriptor for it.
	 */
	if (retval == FSROOT_OK) {
		retval = __fsroot_open(file, flags);
	} else if (retval == FSROOT_E_SYSCALL) {
		if (error != 0 && error_out)
			*error_out = error;
		if (file)
			mm_free(file);
	}

	return retval;
}

int fsroot_open(const char *path, int flags)
{
	int retval = FSROOT_OK;
	int pos;
	struct fsroot_file *file;

	if (!path || !flags)
		return FSROOT_E_BADARGS;

	/* File must exist, due to a previous call to fsroot_create() */
	file = hash_table_get(files, path);
	if (!file || !S_ISREG(file))
		return FSROOT_E_NOTEXISTS;

	__fsroot_open(file, flags);
//	file->num_file_descriptors++;
	return retval;
}

/**
 * Should return the number of bytes read.
 */
int fsroot_read(int fd, char *buf, size_t size, off_t offset, int *error_out)
{
	int retval, error;
	unsigned int idx;
	struct fsroot_file *file;
	struct fsroot_file_descriptor *fildes;

	if (!buf || !size || fd < 0)
		return FSROOT_E_BADARGS;

	pthread_rwlock_rdlock(&open_files.rwlock);

	if (fd >= open_files.num_files) {
		pthread_rwlock_unlock(&open_files.rwlock);
		return FSROOT_E_BADARGS;
	}

	/*
	 * TODO maybe we should add additional sanity checks here,
	 * like checking the PID of the reader.
	 */
	fildes = open_files.file_descriptors[fd];
	pthread_rwlock_unlock(&open_files.rwlock);

	if (!fildes->can_read) {
		/* ERROR: this file was not open for reading */
		retval = FSROOT_E_SYSCALL;
		error = EBADF;
		goto end;
	}
	file = fildes->file;

	if (file->buf == NULL) {
		pthread_rwlock_wrlock(&file->rwlock);
		if (file->buf == NULL)
			retval = fsroot_create_file_buffer(file);
		pthread_rwlock_unlock(&file->rwlock);

		if (retval == -1) {
			retval = FSROOT_E_SYSCALL;
			error = EINVAL;
			goto end;
		} else if (retval == 0 && file->buf == NULL) {
			retval = FSROOT_EOF;
			goto end;
		}
	}

	pthread_rwlock_rdlock(&file->rwlock);
	for (idx = offset; idx < size && idx < file->buf_len; idx++)
		*(buf++) = file->buf[idx];
	pthread_rwlock_unlock(&file->rwlock);

	if (idx == size)
		retval = idx;
	else if (idx == file->buf_len)
		retval = FSROOT_EOF;
end:
	if (retval == FSROOT_E_SYSCALL && error_out)
		*error_out = error;

	return retval;
}

/**
 * Returns the number of bytes written.
 */
int fsroot_write(int fd, char *buf, size_t size, off_t offset, int *error_out)
{
	int retval = FSROOT_OK, error;
	unsigned int idx;
	struct fsroot_file *file;
	struct fsroot_file_descriptor *fildes;

	if (!buf || !size || fd < 0)
		return FSROOT_E_BADARGS;

	pthread_rwlock_rdlock(&open_files.rwlock);

	if (fd >= open_files.num_files) {
		pthread_rwlock_unlock(&open_files.rwlock);
		return FSROOT_E_BADARGS;
	}

	/*
	 * TODO maybe we should add additional sanity checks here,
	 * like checking the PID of the reader.
	 */
	fildes = open_files.file_descriptors[fd];
	pthread_rwlock_unlock(&open_files.rwlock);

	if (!fildes->can_write) {
		/* ERROR: this file was not opened for writing */
		retval = FSROOT_E_SYSCALL;
		error = EBADF;
		goto end_nolock;
	}
	file = fildes->file;

	pthread_rwlock_wrlock(&file->rwlock);

	if (file->buf == NULL) {
		retval = fsroot_create_file_buffer(file);

		if (retval == -1) {
			retval = FSROOT_E_SYSCALL;
			error = EINVAL;
			goto end;
		} else if (retval == 0 && file->buf == NULL) {
			retval = FSROOT_EOF;
			goto end;
		}
		retval = FSROOT_OK;
	}

	if (offset + size >= file->buf_len) {
		/*
		 * Attempting to write past the end of file,
		 * so resize the buffer
		 */
		file->buf_len <<= 1;
		file->buf = mm_realloc(file->buf, file->buf_len);
	}

	for (idx = offset; idx < size; idx++)
		file->buf[idx] = *(buf++);

end:
	pthread_rwlock_unlock(&file->rwlock);
end_nolock:
	if (retval == FSROOT_OK)
		retval = idx;
	else if (retval == FSROOT_E_SYSCALL && error_out)
		*error_out = error;
	return retval;
}

int fsroot_release(const char *path)
{
	int retval = FSROOT_OK;
	struct fsroot_file *file;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file || !S_ISREG(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (!__fsroot_close(file))
		return FSROOT_E_NOTOPEN;

	if (!file->flags.is_synced)
		fsroot_sync_file(file);
	if (file->flags.tmpfile)
		unlink(path);

	if (file->buf) {
		pthread_rwlock_wrlock(&file->rwlock);
		if (file->buf) {
			xfree(file->buf);
			file->buf_len = 0;
		}
		pthread_rwlock_unlock(&file->rwlock);
	}

	return retval;
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
		file = fsroot_create_file(linkpath, uid, gid, mode);
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

	file = fsroot_create_file(path, uid, gid, mode);
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
