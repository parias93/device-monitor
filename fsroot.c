/*
 * fsroot.c
 *
 *  Created on: 18 Nov 2016
 *      Author: Ander Juaristi
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include "hash.h"
#include "mm.h"

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
		int sync      : 1;
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

static struct hash_table *files = NULL;

static struct {
	struct fsroot_file_descriptor **file_descriptors;
	size_t num_files;
	size_t num_slots;
#define OPEN_FILES_INITIAL_NUM_SLOTS 5
	pthread_rwlock_t rwlock;
} open_files;

static char root_path[PATH_MAX];
static unsigned int root_path_len;

static int fsroot_fullpath(const char *in, char *out, size_t outlen)
{
#define FIRST_CHAR(v) (v[0])
#define LAST_CHAR(v) (v[v##_len - 1])
	char must_add_slash = 0, slash = '/';

	if (!in || !out || outlen == 0)
		return 0;

	size_t in_len = strlen(in);
	size_t ttl_len = root_path_len + in_len + 1;

	if (LAST_CHAR(root_path) != '/' && FIRST_CHAR(in) != '/') {
		ttl_len++;
		must_add_slash = 1;
	} else if (LAST_CHAR(root_path) == '/' && FIRST_CHAR(in) == '/') {
		in++;
		ttl_len--;
	}

	if (outlen < ttl_len)
		return 0;

	strcpy(out, root_path);
	if (must_add_slash)
		strncat(out, &slash, 1);
	strcat(out, in);

	fprintf(stderr, "DEBUG: fullpath = %s\n", out);
	return 1;
#undef FIRST_CHAR
#undef LAST_CHAR
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

	fseek(fp, 0, SEEK_END);
	offset = ftell(fp);
	if (offset == -1)
		goto error;
	if (offset == 0)
		goto end;

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
		mm_free(file->buf);
	if (fp)
		fclose(fp);
	if (error_out)
		*error_out = errno;
	return -1;
}

/*
 * Returns a negative number on error,
 * or else the file descriptor.
 * FIXME Maybe on Android we should not allocate a PATH_MAX chunk
 */
static struct fsroot_file *fsroot_create_file(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_file *file = mm_new0(struct fsroot_file);
	char *fullpath = mm_new0n(PATH_MAX);

	if (!fsroot_fullpath(path, fullpath, PATH_MAX)) {
		mm_free(file);
		goto end;
	}

	file->path = fullpath;
	file->uid = uid;
	file->gid = gid;
	file->mode = mode;

end:
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

	file->flags.is_synced = 1;
end:
	return retval;
}

static size_t __fsroot_open(struct fsroot_file *file, int flags)
{
	struct fsroot_file_descriptor *fildes = mm_new0(struct fsroot_file_descriptor);
	size_t retval;

	if ((flags & O_WRONLY) == O_WRONLY) {
		fildes->can_write = 1;
	} else if ((flags & O_RDWR) == O_RDWR) {
		fildes->can_read = 1;
		fildes->can_write = 1;
	} else {
		/* O_RDONLY is 0, which means you can always read */
		fildes->can_read = 1;
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

/*
 * Returns the number of file descriptors that were referring to
 * the file.
 */
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

	if (num_deleted_files == 0)
		goto end;

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
			mm_free(fildes);
	}

	mm_free(open_files.file_descriptors);
	open_files.file_descriptors = file_descriptors;
	open_files.num_files = num_files;
	pthread_rwlock_unlock(&open_files.rwlock);

end:
	return num_deleted_files;
}

static int __fsroot_release(struct fsroot_file *file, char strict)
{
	if (!__fsroot_close(file) && strict)
		return FSROOT_E_NOTOPEN;

	if (file->buf) {
		pthread_rwlock_wrlock(&file->rwlock);
		if (file->buf) {
			mm_free(file->buf);
			file->buf_len = 0;
		}
		pthread_rwlock_unlock(&file->rwlock);
	}

	return FSROOT_OK;
}

/**
 * \param[in] path Relative path to a file
 * \param[in] uid UID of the owner
 * \param[in] gid GID of the owner
 * \param[in] mode File mode (`mode_t`)
 * \param[in] flags File creation flags (see `open(2)`)
 * \param[out] error_out pointer to an integer where the value of `errno` will be placed, on error
 * \return a positive or zero file descriptor to the created file on success, or a negative integer on error
 *
 * Creates a new file and opens it, as if it was followed by a call to fsroot_open().
 *
 * \p path must not exist with fsroot. If an existing path is passed, fsroot_create() returns
 * immediately `FSROOT_E_EXISTS` and goes no further.
 *
 * \p mode should specify a regular file. Non-regular files, such as symlinks, directories or TTYs
 * will be rejected, causing fsroot_create() to return `FSROOT_E_BADARGS`.
 *
 * If the call to the underlying OS services fails, or if some invalid flags are passed that otherwise
 * prevent this function from running correctly (such as passing the `O_DIRECTORY` flag), `FSROOT_E_SYSCALL`
 * is returned, and the value of the `errno` variable will be placed in the memory pointed to by \p error_out,
 * if provided.
 *
 * The following flags are invalid, and will cause fsroot_create() to return `FSROOT_E_SYSCALL`:
 *  - O_ASYNC
 *  - O_DIRECTORY
 *  - O_NOCTTY
 *  - O_NOFOLLOW
 */
int fsroot_create(const char *path, uid_t uid, gid_t gid, mode_t mode, int flags, int *error_out)
{
	int error = 0, retval = FSROOT_OK, fd;
	struct fsroot_file *file;

	if (!path || !mode || !S_ISREG(mode))
		return FSROOT_E_BADARGS;
	if (hash_table_contains(files, path))
		return FSROOT_E_EXISTS;

	/* These flags are not valid, and we'll return an error if we find them */
	if ((flags & O_ASYNC) == O_ASYNC) {
		error = EINVAL;
		retval = FSROOT_E_SYSCALL;
	} else if ((flags & O_DIRECTORY) == O_DIRECTORY) {
		/*
		 * fsroot_create() is only used for regular files.
		 * Directories are opened with fsroot_opendir().
		 */
		error = ENOTDIR;
		retval = FSROOT_E_SYSCALL;
	} else if ((flags & O_NOCTTY) == O_NOCTTY) {
		error = EINVAL;
		retval = FSROOT_E_SYSCALL;
	} else if ((flags & O_NOFOLLOW) == O_NOFOLLOW) {
		/* FIXME: O_NOFOLLOW should be ignored if O_PATH is present */
		error = ELOOP;
		retval = FSROOT_E_SYSCALL;
	}

	if (retval != FSROOT_OK)
		goto end;

	file = fsroot_create_file(path, uid, gid, mode);
	if (!file) {
		error = EINVAL;
		retval = FSROOT_E_SYSCALL;
		goto end;
	}
	/*
	 * These flags will be handled internally:
	 * 	- O_EXCL
	 * 	- O_APPEND
	 * 	- O_DSYNC
	 * 	- O_SYNC
	 * 	- O_TMPFILE
	 *
	 * Thus, if they're present in 'flags', we strip them out.
	 *
	 * We also strip out O_EXCL and O_CREAT, since we pass them to open(2) anyway.
	 */
	if ((flags & O_EXCL) == O_EXCL)
		flags ^= O_EXCL;
	if ((flags & O_CREAT) == O_CREAT)
		flags ^= O_CREAT;
//	TODO this is not needed since we're not handling file offsets ourselves in the end
//	if (flags & O_APPEND == O_APPEND) {
//		flags ^= O_APPEND;
//		file->flags.append = 1;
//	}
	/*
	 * FIXME
	 * The difference between O_SYNC and O_DSYNC is that the former also writes
	 * file metadata. We're leaving this for later and treat both identically,
	 * as we're keeping all file metadata in memory for now.
	 */
#ifdef O_DSYNC
	if ((flags & O_DSYNC) == O_DSYNC) {
		flags ^= O_DSYNC;
		file->flags.sync = 1;
	}
#endif
#ifdef O_SYNC
	/* In Linux, O_SYNC is equivalent to O_RSYNC */
	if ((flags & O_SYNC) == O_SYNC) {
		flags ^= O_SYNC;
		file->flags.sync = 1;
	}
#endif
#ifdef O_TMPFILE
	if (flags & O_TMPFILE == O_TMPFILE) {
		flags ^= O_TMPFILE;
		file->flags.tmpfile = 1;
	}
#endif

	/* Now issue the system call */
	fd = open(file->path, O_CREAT | O_EXCL | flags, 0100600);
	if (fd == -1) {
		error = errno;
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

/**
 * \param[in] path Relative path to a file
 * \param[in] flags Flags (see `open(2)`)
 * \return a positive file descriptor on success, or a negative integer on error
 *
 * \p path must specify an already existing file. If the file does not exist,
 * fsroot_open() immediately returns `FSROOT_E_NOTEXISTS` and goes no further.
 *
 * This function does not create new files. Use fsroot_create() for that instead.
 * This function will not complain if file creation (`O_CREAT`, `O_EXCL` )
 * or truncation (`O_TRUNC`) flags are passed, but they will be completely ignored.
 */
int fsroot_open(const char *path, int flags)
{
	struct fsroot_file *file;

	if (!path)
		return FSROOT_E_BADARGS;

	/* File must exist, due to a previous call to fsroot_create() */
	file = hash_table_get(files, path);
	if (!file || !S_ISREG(file->mode))
		return FSROOT_E_NOTEXISTS;

	return __fsroot_open(file, flags);
}

/**
 * \param[in] fd a valid file descriptor, obtained by a previous call to fsroot_create() or fsroot_open()
 * \param[out] buf pointer to a buffer where the read data will be placed
 * \param[in] size number of bytes to read
 * \param[in] offset offset to start reading from
 * \param[out] error_out pointer to an integer where the value of `errno` will be placed, on error
 * \return the number of bytes read, or a negative error code on error
 *
 * This function will read up to \p size bytes from the file referred to by file descriptor \p fd
 * starting at offset \p offset, and place the content in the buffer pointed to by \p buf.
 * The caller must supply a buffer of at least \p size length.
 *
 * This function returns the number of bytes read from the file and placed into \p buf,
 * which might be less than \p size. This should happen when the end of the file is reached
 * before \p size bytes were read. If this happens, `FSROOT_EOF` will be placed in \p error_out
 * (even though a positive number was returned).
 *
 * If the file was not open for reading (neither `O_RDONLY` nor `O_RDWR` were passed to
 * fsroot_create() or fsroot_open()) then `FSROOT_E_SYSCALL` will be returned and \p error_out
 * will be set to `EBADF`. If an error happens in some of the underlying OS services, `FSROOT_E_SYSCALL`
 * is returned and \p error_out is set to the value of `errno`.
 *
 * If an error happens, \p buf will not be modified in any way.
 */
int fsroot_read(int fd, char *buf, size_t size, off_t offset, int *error_out)
{
	int retval, error = 0;
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
			retval = fsroot_create_file_buffer(file, &error);
		pthread_rwlock_unlock(&file->rwlock);

		if (retval == -1) {
			retval = FSROOT_E_SYSCALL;
			goto end;
		} else if (retval == 0 && file->buf == NULL) {
			error = FSROOT_EOF;
			goto end;
		}
	}

	pthread_rwlock_rdlock(&file->rwlock);
	for (idx = offset; idx < size && idx < file->buf_len; idx++)
		*(buf++) = file->buf[idx];
	pthread_rwlock_unlock(&file->rwlock);

	retval = idx;
	if (idx < size)
		error = FSROOT_EOF;

end:
	if (error_out && error != 0)
		*error_out = error;

	return retval;
}

/**
 * \param[in] fd a valid file descriptor, obtained by a previous call to fsroot_create() or fsroot_open()
 * \param[in] buf pointer to a buffer to take data from
 * \param[in] size length of the buffer
 * \param[in] offset offset to start writing from
 * \param[out] error_out pointer to an integer where the value `errno` will be placed, on error
 * \return the number of bytes written, or a negative error code on error
 *
 * This function will write \p size bytes from \p buf to the file referred to by \p fd, starting
 * at offset \p offset. If the end of the file is reached, the file is resized until, at least,
 * the remaining bytes can be written.
 *
 * This function returns the number of bytes written from \p buf to the file.
 *
 * If the file was not open for writing () then `FSROOT_E_SYSCALL` is returned and \p error_out
 * will be set to `EBADF`. If an error happens in some of the underlying OS services, `FSROOT_E_SYSCALL`
 * is returned and \p error_out is set to the value of `errno`.
 *
 * If an error happens, the file will not be modified in any way.
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
		retval = fsroot_create_file_buffer(file, &error);

		if (retval == -1) {
			retval = FSROOT_E_SYSCALL;
			goto end;
		}
		retval = FSROOT_OK;
	}

	if (offset + size >= file->buf_len) {
		/*
		 * Attempting to write past the end of file,
		 * so resize the buffer. Beware file->buf might be NULL here.
		 * If we pass a NULL pointer to mm_realloc() it should just behave like
		 * malloc(). But this is not guaranteed for older implementations, and it is
		 * cheap to guard against this, so let's do it.
		 */
		if (file->buf_len == 0) {
			file->buf_len = offset + size;
		} else {
			while (offset + size >= file->buf_len)
				file->buf_len <<= 1;
		}

		file->buf = (file->buf ?
				mm_realloc(file->buf, file->buf_len) :
				mm_malloc0(file->buf_len));
	}

	for (idx = offset; idx < size + offset; idx++)
		file->buf[idx] = *(buf++);

	file->flags.is_synced = 0;
	if (file->flags.sync)
		fsroot_sync_file(file);

end:
	pthread_rwlock_unlock(&file->rwlock);
end_nolock:
	if (retval == FSROOT_OK)
		retval = idx;
	else if (retval == FSROOT_E_SYSCALL && error_out)
		*error_out = error;
	return retval;
}

/**
 * \param[in] path Relative path to a file
 * \returns `FSROOT_OK` on success, or a negative integer on error
 *
 * Makes sure the file specified by path has been fully written to the
 * underlying hardware media.
 *
 * If the specified file does not exists, or is not a regular file,
 * then this function returns `FSROOT_E_NOTEXISTS`.
 */
int fsroot_sync(const char *path)
{
	struct fsroot_file *file;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file || !S_ISREG(file->mode))
		return FSROOT_E_NOTEXISTS;

	return fsroot_sync_file(file);
}

/**
 * \param[in] path Relative path to a file
 * \return `FSROOT_OK` on success, or a negative integer on error
 *
 * This function will destroy all the open file descriptors for the
 * specified by the path \p path. All the file descriptors for this file
 * (obtained with fsroot_create() and fsroot_open()) will no longer be valid,
 * and **calling fsroot_read() and fsroot_write() with either of these will have
 * undefined effects**.
 *
 * This function will sync the file to the underlying hardware media. If the file was marked
 * as temporary (`O_TMPFILE` was passed to fsroot_create() or fsroot_open()) the file is removed.
 *
 * If the specified file does not exist this function returns `FSROOT_E_NOTEXISTS`.
 * If the file exists but there are no file descriptors associated with it this function
 * returns `FSROOT_E_NOTOPEN`.
 */
int fsroot_release(const char *path)
{
	int retval = FSROOT_OK;
	struct fsroot_file *file;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file || !S_ISREG(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (file->flags.tmpfile)
		unlink(path);
	else if (!file->flags.is_synced)
		fsroot_sync_file(file);

	retval = __fsroot_release(file, 1);
	if (retval != FSROOT_OK)
		goto end;

end:
	return retval;
}

int fsroot_getattr(const char *path, struct stat *out_st)
{
	struct stat st;
	struct fsroot_file *file;

	if (!path || !out_st)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	if (stat(path, &st) == -1)
		return FSROOT_E_SYSCALL;

	st.st_mode = file->mode;
	st.st_uid = file->uid;
	st.st_gid = file->gid;
	memcpy(out_st, &st, sizeof(struct stat));

	return FSROOT_OK;
}

/**
 * \param[in] linkpath Relative path to the link
 * \param[in] target Relative path to the link target (file the link will point to)
 * \param[in] uid UID for the symbolic link
 * \param[in] gid GID for the symbolic link
 * \param[in] mode Mode for the symbolic link
 * \return `FSROOT_OK` on success or a negative value on error
 *
 * Creates a symbolic link.
 *
 * Parameter \p mode is basically there to specify the permission bits of the symlink.
 * This function will check whether \p mode effectively describes a symlink (with `S_ISLNK(mode)`)
 * and will fail returning `FSROOT_E_BADARGS` otherwise.
 *
 * If successful, a symlink will be created pointing to the file \p target (regardless of whether
 * it exists or not) with the specified UID, GID and permissions.
 *
 * This function relies on the `symlink(2)` libc function to create the symlink. If the call to `symlink(2)`
 * fails, `FSROOT_E_SYSCALL` is returned.
 *
 * If \p linkpath already exists, regardless of whether it is a symbolic link or not,
 * this function goes no further and returns `FSROOT_E_EXISTS`.
 */
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

	if (strlen(target) >= LONG_MAX)
		return FSROOT_E_NOMEM;
	if (symlink(target, full_linkpath) == -1)
		retval = FSROOT_E_SYSCALL;

	if (retval == FSROOT_OK) {
		/* symlink was created successfully, so we register it in our hash table */
		file = fsroot_create_file(linkpath, uid, gid, mode);
		hash_table_put(files, linkpath, file);
	}

	return retval;
}

/**
 * \param[in] linkpath Relative path to a symbolic link
 * \param[in] dst Caller-supplied pointer to a buffer
 * \param[in] dstlen Pointer to an integer with the size of the buffer pointed to by \p dst
 * \return `FSROOT_OK` on success or a negative value on error
 *
 * Reads the target of a symbolic link (the path of the file it points to) and stores it
 * in \p buf. Unlike `lstat(2)`, fsroot_readlink() does append a NULL terminator at the end.
 *
 * If \p buf does not have enough space to store the target of the symbolic link
 * and a NULL terminator (as specified by \p *dstlen) this function stores in
 * the integer pointed to by \p dstlen the minimum length required to store the target path
 * and a NULL terminator, and then returns `FSROOT_E_NOMEM`.
 *
 * If \p linkpath does not exist or it is not a symbolic link, the function
 * returns `FSROOT_E_NOTEXISTS` without touching \p dst or \p dstlen.
 */
int fsroot_readlink(const char *linkpath, char *dst, size_t *dstlen)
{
	char full_linkpath[PATH_MAX];
	struct stat st;
	size_t required_size, actual_len;
	struct fsroot_file *file;

	if (!linkpath || !dst || !dstlen ||
			!fsroot_fullpath(linkpath, full_linkpath, sizeof(full_linkpath)))
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, linkpath);
	if (file == NULL || !S_ISLNK(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (lstat(full_linkpath, &st) == -1)
		return FSROOT_E_SYSCALL;
	if (st.st_size >= LONG_MAX)
		return FSROOT_E_NOMEM;

	required_size = st.st_size + 1;
	if (*dstlen < required_size) {
		*dstlen = required_size;
		return FSROOT_E_NOMEM;
	}

	actual_len = readlink(full_linkpath, dst, *dstlen);
	if (actual_len == -1)
		return FSROOT_E_SYSCALL;

	dst[actual_len] = 0;
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

void fsroot_deinit()
{
	hash_table_iterator iter;
	pthread_rwlock_wrlock(&open_files.rwlock);

	for (unsigned int i = 0; i < open_files.num_files; i++)
		mm_free(open_files.file_descriptors[i]);

	mm_free(open_files.file_descriptors);
	open_files.num_slots = 0;
	open_files.num_files = 0;

	if (files) {
		for (hash_table_iterate(files, &iter); hash_table_iter_next(&iter);) {
			__fsroot_release(iter.value, 0);
	//		hash_table_remove(files, iter.key);
			mm_free(iter.value);
		}

		hash_table_destroy(files);
	}

	pthread_rwlock_unlock(&open_files.rwlock);
	pthread_rwlock_destroy(&open_files.rwlock);
}

int fsroot_init(const char *root)
{
	files = NULL;
	memset(&open_files, 0, sizeof(open_files));

	if (!root)
		return FSROOT_E_BADARGS;

	/* TODO how do we balance the space? (eg. Android) */
	files = make_string_hash_table(10);

	open_files.num_files = 0;
	open_files.num_slots = OPEN_FILES_INITIAL_NUM_SLOTS;
	open_files.file_descriptors = mm_mallocn0(open_files.num_slots,
			sizeof(struct fsroot_file_descriptor *));
	pthread_rwlock_init(&open_files.rwlock, NULL);

	root_path_len = strlen(root);
	if (root_path_len > sizeof(root_path) - 1)
		goto error_nomem;
	strcpy(root_path, root);

	return FSROOT_OK;
error_nomem:
	fsroot_deinit();
	return FSROOT_E_NOMEM;
}
