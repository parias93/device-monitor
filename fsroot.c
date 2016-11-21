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
#include "hash.h"

/*
 * TODO
 *  - Implement fsroot_getattr()
 *  - Get the umask for the user calling us
 *  - What happens when we have "../../..", etc?
 *  - Array & memory management utilities
 */
/* TODO review these values */
#define FSROOT_MORE				 1
#define FSROOT_OK				 0
#define FSROOT_E_BADARGS			-1
#define FSROOT_E_EXISTS				-2
#define FSROOT_E_NOTEXISTS			-3
#define FSROOT_E_NOMEM				-4
#define FSROOT_E_NONEMPTY			-5
#define FSROOT_E_NEW_DIRECTORY_NOTEXISTS	-6

struct fsroot_file {
	const char *name;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	/* TODO this should not be public */
	/* TODO maybe this should be an 'fsroot_directory' */
	struct fsroot_file *parent;
};

/* TODO maybe we could directly define the fields here? */
#define FSROOT_FILE struct fsroot_file __file
//#define name (__file.name)
//#define mode (__file.mode)
//#define uid  (__file.uid)
//#define gid  (__file.gid)

struct fsroot_symlink {
	FSROOT_FILE;
	struct fsroot_file *target;
};

struct fsroot_directory {
	FSROOT_FILE;
	struct fsroot_file **entries;
	size_t num_entries;
};

/* TODO what does 'static' do here? */
static struct hash_table *files;

/* TODO get this out of here */
/* TODO we should check return value of calloc() != NULL */
#define xnew0(type) calloc(1, sizeof(type))
#define xnew0n(n, type) calloc(n, sizeof(type))
#define xfree(ptr)  do { if (ptr) free(ptr); ptr = (void *) 0; } while (0)

static char **fsroot_path_split(const char *ppath, size_t *outlen)
{
	const char *path = ppath + strlen(ppath) - 1;
	size_t parts_len = 10, count = 0, idx = 0;
	char **parts = xnew0n(parts_len, char *);

	while (path >= ppath) {
		while (*path == '/') {
			path--;
			if (path == ppath)
				goto fail;
		}
		while (*path != '/') {
			if (*path <= 0x20 || *path > 0x7E)
				goto fail;

			count++;
			path--;

			if (path == ppath)
				goto fail;
		}

		parts[idx++] = strndup(path, count);
		if (idx == parts_len) {
			parts_len <<= 1;
			parts = realloc(parts, sizeof(char *) * parts_len);
		}

		count = 0;
	}

	if (idx == 0)
		goto fail;

	*outlen = idx;
	return parts;
fail:
	free(parts);
	return NULL;
}

static size_t *fsroot_compute_path_indexes(const char **path, size_t len, size_t *outlen)
{
	size_t idx = 0, count = 0;
	size_t *indexes = xnew0n(len, size_t);

	while (idx < len) {
		if (strcmp(path[idx], "..") == 0) {
			idx++;
			/* We can't let count wrap around itself */
			if (count-- == 0)
				goto fail;
			continue;
		} else if (strcmp(path[idx], ".") == 0) {
			idx++;
			continue;
		}

		if (count == len) {
			len <<= 1;
			indexes = realloc(indexes, sizeof(unsigned int) * len);
		}

		indexes[count++] = idx++;
	}

	if (count == 0)
		goto fail;

	*outlen = count;
	return indexes;
fail:
	free(indexes);
	return NULL;
}

static char *fsroot_full_path(const char **path, size_t *indexes, size_t indexes_len)
{
	const char slash = '/';
	char *fullpath = NULL;
	size_t fullpath_len = 1; /* Count the NULL terminator */

	for (size_t i = 0; i < indexes_len; i++) {
		size_t cur_index = indexes[i];
		fullpath_len += strlen(path[cur_index]) + 1; /* +1 for the trailing slash ('/') */
	}

	fullpath = xnew0n(fullpath_len, char);

	for (size_t i = 0; i < indexes_len; i++) {
		size_t cur_index = indexes[i];
		strcat(fullpath, path[cur_index]);
		strncat(fullpath, &slash, 1);
	}

	return fullpath;
}

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

void fsroot_getattr()
{
	// TODO implement
	return;
}

static int __fsroot_create_file(struct fsroot_directory *dir, struct fsroot_file *file)
{
	if (dir) {
		/*
		 * TODO
		 * This should check we have enough space
		 * and resize the array if needed!
		 */
		dir->entries[dir->num_entries] = file;
		dir->num_entries++;

		file->parent = (struct fsroot_file *) dir;
	} else {
		file->parent = NULL;
	}

	return 0;
}

static struct fsroot_file *fsroot_create_file(struct fsroot_directory *dir, const char *name, uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_file *file = xnew0(struct fsroot_file);

	file->name = strdup(name);
	file->uid = uid;
	file->gid = gid;
	file->mode = 0100000 | mode;

	if (dir) {
		file->parent = (struct fsroot_file *) dir;
		__fsroot_create_file(dir, file);
	}

	return file;
}

static struct fsroot_directory *fsroot_create_directory(struct fsroot_directory *parent_dir, const char *name, uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_directory *dir = xnew0(struct fsroot_directory);

	dir->__file.name = strdup(name);
	dir->__file.uid = uid;
	dir->__file.gid = gid;
	dir->__file.mode = 0040000 | mode;

	/* TODO should use array management functions here */
	dir->entries = xnew0n(10, struct fsroot_file *);
	dir->num_entries = 0;

	if (parent_dir) {
		dir->__file.parent = (struct fsroot_file *) parent_dir;
		__fsroot_create_file(parent_dir, (struct fsroot_file *) dir);
	}

	return dir;
}

static int fsroot_remove_file(struct fsroot_file *file)
{
	int i, j;
	struct fsroot_directory *dir = (struct fsroot_directory *) file->parent;

	if (dir) {
		/* Get to the file we're looking for */
		for (i = 0; i < dir->num_entries; i++) {
			if (dir->entries[i] == file)
				break;
		}

		if (i < dir->num_entries) {
			for (j = i; j < dir->num_entries; j++) {
				if (j + 1 < dir->num_entries)
					dir->entries[j] = dir->entries[j + 1];
			}
		} else {
			/*
			 * File not found. This should not happen.
			 * TODO what to do here?
			 */
		}
	}
}

int fsroot_symlink(const char *plink, const char *ppath, uid_t uid, gid_t gid)
{
	struct fsroot_symlink *file;
	char *link, *path;

	if (!plink || !ppath)
		return FSROOT_E_BADARGS;

	link = strdup(plink);
	path = strdup(ppath);
	if (!link || !path)
		return FSROOT_E_NOMEM;

	if (hash_table_contains(files, link))
		return FSROOT_E_EXISTS;
	if (!hash_table_contains(files, path))
		return FSROOT_E_NOTEXISTS;

	/* The symlink points to the *relative* path */
	//file = NEW_SYMLINK(full_link, path);
	file = xnew0(struct fsroot_symlink);
	file->__file.name = link;
	/* TODO should check that the user and group exist */
	file->__file.uid = uid;
	file->__file.gid = gid;
	file->__file.mode = 0120000 | fsroot_umask();
	file->target->name = path;

	hash_table_put(files, link, file);
	xfree(file);

	return FSROOT_OK;
}

int fsroot_readlink(const char *path, char *dst, size_t dstlen)
{
	struct fsroot_symlink *file;

	if (!path || !dst || dstlen == 0)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (file == NULL || !S_ISLNK(file->__file.mode))
		return FSROOT_E_NOTEXISTS;

	if (strlen(file->target->name) + 1 > dstlen)
		return FSROOT_E_NOMEM;
	strcpy(dst, file->target->name);

	return FSROOT_OK;
}

int fsroot_mkdir(const char *ppath, uid_t uid, gid_t gid)
{
	struct fsroot_directory *file;
	char *path;

	if (!ppath)
		return FSROOT_E_BADARGS;

	path = strdup(ppath);
	if (!path)
		return FSROOT_E_NOMEM;

	if (hash_table_contains(files, path))
		return FSROOT_E_EXISTS;

	//file = NEW_DIRECTORY(fullpath);
	file = xnew0(struct fsroot_directory);
	file->__file.name = path;
	/* TODO should check that the user and group exist */
	file->__file.uid = uid;
	file->__file.gid = gid;
	file->__file.mode = 0040000 | fsroot_umask();
	hash_table_put(files, path, file);
	xfree(file);

	return FSROOT_OK;
}

int fsroot_rmdir(const char *path)
{
	struct fsroot_directory *file;
	char fullpath[PATH_MAX];

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (file == NULL || !S_ISDIR(file->__file.mode))
		return FSROOT_E_NOTEXISTS;

	/*
	 * We cannot remove nonempty directories
	 */
	if (file->num_entries > 0)
		return FSROOT_E_NONEMPTY;

	hash_table_remove(files, path);
	xfree(file);
	return FSROOT_OK;
}

/*
 * Works like rename(2).
 * Moves the renamed file between directories if required.
 * Also, if a directory component of 'newpath' does not exist, ENOENT should be returned.
 */
int fsroot_rename(const char *path, const char *pnewpath)
{
	struct fsroot_file *file;
	struct fsroot_directory *dst_directory;
	char full_path[PATH_MAX], full_newpath[PATH_MAX];
	char *newpath;
	char **newpath_parts;
	size_t *newpath_indexes;
	size_t parts_len, indexes_len;

	if (!path || !pnewpath)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (!file)
		return FSROOT_E_NOTEXISTS;

	if (hash_table_contains(files, pnewpath))
		return FSROOT_E_EXISTS;

	/*
	 * TODO maybe we could optimize this by checking whether the
	 * original and new directory are the same
	 */

	/* Tokenize newpath */
	newpath_parts = fsroot_path_split(pnewpath, &parts_len);
	if (!newpath_parts || !parts_len)
		return FSROOT_E_BADARGS;

	newpath_indexes = fsroot_compute_path_indexes((const char **) newpath_parts, parts_len, &indexes_len);
	if (!newpath_indexes || !indexes_len)
		return FSROOT_E_BADARGS;

	/* Do not count for the base name */
	indexes_len--;
	/* Compose full path, without the base name (eg. the full destination directory) */
	newpath = fsroot_full_path((const char **) newpath_parts, newpath_indexes, indexes_len);
	dst_directory = hash_table_get(files, newpath);
	if (!dst_directory || !S_ISDIR(dst_directory->__file.mode)) {
		/* The target directory does not exist. This is an error. */
		return FSROOT_E_NEW_DIRECTORY_NOTEXISTS;
	}
	free(newpath_indexes);
	free(newpath_parts);

	/*
	 * Tell the parent directory that this file is no longer
	 * part of it. Then remove it from the hash table.
	 */
	fsroot_remove_file(file);
	hash_table_remove(files, path);

	/*
	 * Now add the file to the new destination directory.
	 * Then put the whole path in the hash table.
	 */
	__fsroot_create_file(dst_directory, file);
	hash_table_put(files, newpath, file);

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
	if (filetype && ((file->mode & 0170000) != filetype))
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

	/* TODO should check that the user and group exist */
	file->uid = uid;
	file->gid = gid;
	return FSROOT_OK;
}

/*
 * We return a 'fsroot_file' to the user rather than a 'fsroot_directory', because
 * we do not want them to tinker with the directory's fields, such as num_entries.
 */
int fsroot_opendir(const char *path, struct fsroot_file **outdir)
{
	int retval;
	struct fsroot_directory *dir;

	if (!path || !outdir)
		return FSROOT_E_BADARGS;

	dir = hash_table_get(files, path);
	if (dir && S_ISDIR(dir->__file.mode)) {
		*outdir = (struct fsroot_file *) dir;
		retval = FSROOT_OK;
	} else {
		retval = FSROOT_E_NOTEXISTS;
	}

	return retval;
}

int fsroot_readdir(off_t offset, struct fsroot_file *directory, struct fsroot_file *file)
{
	int retval;
	const char *path;
	struct fsroot_directory *dir;

	if (!directory || !file)
		return FSROOT_E_BADARGS;

	dir = (struct fsroot_directory *) directory;

	if (offset >= dir->num_entries) {
		retval = FSROOT_OK;
	} else {
		file->name = dir->entries[offset]->name;
		file->mode = dir->entries[offset]->mode;
		file->uid = dir->entries[offset]->uid;
		file->gid = dir->entries[offset]->gid;
		retval = FSROOT_MORE;
	}

	return retval;
}

int main()
{
	files = make_string_hash_table(10);

	/*
	 * Hierarchy:
	 * 	/test		file
	 * 	/foo		dir
	 * 	/foo/test	file
	 * 	/bar/baz	dir
	 * 	/bar/baz/test	file
	 */
	struct fsroot_directory *dir_foo = fsroot_create_directory(NULL, "foo", 1000, 1000, 0),
		*dir_bar = fsroot_create_directory(NULL, "bar", 1000, 1000, 0),
		*dir_baz = fsroot_create_directory(dir_bar, "baz", 1000, 1000, 0);

	hash_table_put(files, "/foo", dir_foo);
	hash_table_put(files, "/bar", dir_bar);
	hash_table_put(files, "/bar/baz", dir_baz);

	struct fsroot_file *test = fsroot_create_file(NULL, "test", 1000, 1000, 0),
		*test_foo = fsroot_create_file(dir_foo, "test", 1000, 1000, 0),
		*test_baz = fsroot_create_file(dir_baz, "test", 1000, 1000, 0);

	hash_table_put(files, "/test", test);
	hash_table_put(files, "/foo/test", test_foo);
	hash_table_put(files, "/bar/baz/test", test_baz);

	hash_table_destroy(files);
	return 0;
}
