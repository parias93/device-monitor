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
#include "mm.h"

/*
 * TODO
 *  - Implement fsroot_getattr()
 *  - Get the umask for the user calling us
 *  - Check that user & group exist
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

#define FSROOT_FILE		\
	char *name;		\
	mode_t mode;		\
	uid_t uid;		\
	gid_t gid;		\
	struct fsroot_directory *parent_dir

struct fsroot_symlink {
	FSROOT_FILE;
	const char *target;
};

struct fsroot_directory {
	FSROOT_FILE;
	struct fsroot_file **entries;
	size_t num_entries;
	size_t num_slots;
};

struct fsroot_file {
	FSROOT_FILE;
};

//#define name (__file.name)
//#define mode (__file.mode)
//#define uid  (__file.uid)
//#define gid  (__file.gid)

/* TODO what does 'static' do here? */
static struct hash_table *files;

static void fsroot_invert_array(const char **arr, size_t len)
{
	const char *tmp;
	for (size_t i = 0; i < (len / 2); i++) {
		tmp = arr[i];
		arr[i] = arr[len - i - 1];
		arr[len - i - 1] = tmp;
	}
}

static char **fsroot_path_split(const char *ppath, size_t *outlen)
{
	const char *path = ppath + strlen(ppath) - 1;
	size_t parts_len = 10, count = 0, idx = 0;
	char **parts = mm_new(parts_len, char *);

	while (path > ppath) {
		while (*path == '/') {
			path--;
			if (path < ppath)
				goto fail;
		}
		while (*path != '/') {
			if (*path <= 0x20 || *path > 0x7E)
				goto fail;

			count++;
			path--;

			if (path < ppath)
				goto fail;
		}

		parts[idx++] = strndup(path + 1, count);
		if (idx == parts_len) {
			parts_len <<= 1;
			parts = mm_reallocn(parts, parts_len, sizeof(char *));
		}

		count = 0;
	}

	if (idx == 0)
		goto fail;

	fsroot_invert_array((const char **) parts, idx);
	*outlen = idx;
	return parts;
fail:
	free(parts);
	return NULL;
}

static size_t *fsroot_compute_path_indexes(const char **path, size_t len, size_t *outlen)
{
	size_t idx = 0, count = 0;
	size_t *indexes = mm_new(len, size_t);

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
			indexes = mm_reallocn(indexes, len, sizeof(size_t));
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
	size_t fullpath_len = 2; /* Count the NULL terminator, and the initial '/' */

	for (size_t i = 0; i < indexes_len; i++) {
		size_t cur_index = indexes[i];
		fullpath_len += strlen(path[cur_index]) + 1; /* +1 for the trailing slash ('/') */
	}

	fullpath = mm_new(fullpath_len, char);
	fullpath[0] = slash;

	for (size_t i = 0; i < indexes_len; i++) {
		size_t cur_index = indexes[i];
		strcat(fullpath, path[cur_index]);
		if (i + 1 < indexes_len)
			strncat(fullpath, &slash, 1);
	}

	return fullpath;
}

static char *fsroot_get_directory(const char *path)
{
	char *fullpath;
	char **parts;
	size_t *indexes = NULL;
	size_t parts_len, indexes_len;

	parts = fsroot_path_split(path, &parts_len);
	if (!parts || !parts_len)
		goto error;
	if (parts_len == 1) {
		fullpath = "/";
		goto end;
	}

	indexes = fsroot_compute_path_indexes((const char **) parts, parts_len - 1, &indexes_len);
	if (!indexes || !indexes_len)
		goto error;

	fullpath = fsroot_full_path((const char **) parts, indexes, indexes_len);

end:
	for (size_t i = 0; i < parts_len; i++)
		mm_free(parts[i]);
	mm_free(indexes);
	mm_free(parts);

	return fullpath;
error:
	return NULL;
}

static char *fsroot_get_basename(const char *path)
{
	char *basename = NULL;
	char **parts = NULL;
	size_t *indexes = NULL;
	size_t parts_len, indexes_len;

	parts = fsroot_path_split(path, &parts_len);
	if (!parts || !parts_len)
		goto end;
	if (parts_len == 1) {
		basename = strdup(parts[0]);
		goto end;
	}

	indexes = fsroot_compute_path_indexes((const char **) parts, parts_len, &indexes_len);
	if (!indexes || !indexes_len)
		goto end;

	basename = strdup(parts[indexes[indexes_len - 1]]);

end:
	for (size_t i = 0; i < parts_len; i++)
		mm_free(parts[i]);
	mm_free(indexes);
	mm_free(parts);

	return basename;
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

static void __fsroot_create_file(struct fsroot_directory *dir, struct fsroot_file *file)
{
	if (dir) {
		if (dir->num_entries == dir->num_slots) {
			dir->num_slots <<= 1;
			dir->entries = mm_reallocn(dir->entries, dir->num_slots, sizeof(struct fsroot_file *));
		}
		dir->entries[dir->num_entries++] = file;
		file->parent_dir = dir;
	} else {
		file->parent_dir = NULL;
	}
}

static void __fsroot_create_directory(struct fsroot_directory *parent_dir, struct fsroot_directory *dir)
{
	dir->entries = mm_new(10, struct fsroot_file *);
	dir->num_slots = 10;
	dir->num_entries = 0;

	__fsroot_create_file(parent_dir, (struct fsroot_file *) dir);
}

static struct fsroot_file *fsroot_create_file(struct fsroot_directory *dir, const char *name, uid_t uid, gid_t gid, mode_t mode)
{
	struct fsroot_file *file = NULL;

	switch (mode & S_IFMT) {
	case S_IFREG:
		file = mm_new0(struct fsroot_file);
		__fsroot_create_file(dir, file);
		break;
	case S_IFDIR:
		file = mm_new0(struct fsroot_directory);
		__fsroot_create_directory(dir, (struct fsroot_directory *) file);
		break;
	case S_IFLNK:
		file = mm_new0(struct fsroot_symlink);
		__fsroot_create_file(dir, file);
		break;
	}

	file->name = strdup(name);
	file->uid = uid;
	file->gid = gid;
	file->mode = mode;

	return file;
}

static void fsroot_remove_file(struct fsroot_file *file)
{
	int i, j;
	struct fsroot_directory *dir = (struct fsroot_directory *) file->parent_dir;

	if (dir) {
		/* Get to the file we're looking for */
		for (i = 0; i < dir->num_entries; i++) {
			if (dir->entries[i] == file)
				break;
		}

		if (i < dir->num_entries) {
			for (j = i; j + 1 < dir->num_entries; j++)
				dir->entries[j] = dir->entries[j + 1];

			dir->entries[j] = NULL;
			dir->num_entries--;
			file->parent_dir = NULL;
		}

		/* Else file was not found. This should not happen. */
	}
}

int fsroot_symlink(const char *plink, const char *ppath, uid_t uid, gid_t gid)
{
	struct fsroot_directory *dir = NULL;
	struct fsroot_symlink *file = NULL;
	char *link, *path, *directory;

	if (!plink || !ppath)
		return FSROOT_E_BADARGS;

	link = strdup(plink);
	path = strdup(ppath);
	if (!link || !path)
		return FSROOT_E_NOMEM;

	if (hash_table_contains(files, link))
		return FSROOT_E_EXISTS;

	directory = fsroot_get_directory(link);
	if (strcmp(directory, "/") == 0)
		goto create_file;

	dir = hash_table_get(files, directory);
	if (!dir)
		return FSROOT_E_NEW_DIRECTORY_NOTEXISTS;

create_file:
	file = (struct fsroot_symlink *) fsroot_create_file(dir, link, uid, gid, S_IFLNK);
	/* The symlink points to the *relative* path */
	file->target = path;

	hash_table_put(files, link, file);
	return FSROOT_OK;
}

int fsroot_readlink(const char *path, char *dst, size_t dstlen)
{
	struct fsroot_symlink *file;

	if (!path || !dst || dstlen == 0)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (file == NULL || !S_ISLNK(file->mode))
		return FSROOT_E_NOTEXISTS;

	if (strlen(file->target) + 1 > dstlen)
		return FSROOT_E_NOMEM;
	strcpy(dst, file->target);

	return FSROOT_OK;
}

int fsroot_mkdir(const char *ppath, uid_t uid, gid_t gid)
{
	struct fsroot_directory *file, *dir;
	char *path, *directory;

	if (!ppath)
		return FSROOT_E_BADARGS;

	path = strdup(ppath);
	if (!path)
		return FSROOT_E_NOMEM;

	if (hash_table_contains(files, path))
		return FSROOT_E_EXISTS;

	directory = fsroot_get_directory(path);
	if (strcmp(directory, "/") == 0)
		goto create_file;

	dir = hash_table_get(files, directory);
	if (!dir)
		return FSROOT_E_NEW_DIRECTORY_NOTEXISTS;

create_file:
	file = (struct fsroot_directory *) fsroot_create_file(dir, path, uid, gid, S_IFDIR);
	hash_table_put(files, path, file);

	return FSROOT_OK;
}

int fsroot_rmdir(const char *path)
{
	struct fsroot_directory *file;

	if (!path)
		return FSROOT_E_BADARGS;

	file = hash_table_get(files, path);
	if (file == NULL || !S_ISDIR(file->mode))
		return FSROOT_E_NOTEXISTS;

	/*
	 * We cannot remove nonempty directories
	 */
	if (file->num_entries > 0)
		return FSROOT_E_NONEMPTY;

	fsroot_remove_file((struct fsroot_file *) file);
	hash_table_remove(files, path);
	mm_free(file->name);
	mm_free(file);
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
	struct fsroot_directory *path_dir = NULL, *newpath_dir = NULL;
	char *path_directory, *newpath_directory, *new_basename;

	if (!path || !pnewpath)
		return FSROOT_E_BADARGS;

	new_basename = fsroot_get_basename(pnewpath);
	if (!new_basename)
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

	path_directory = fsroot_get_directory(path);
	newpath_directory = fsroot_get_directory(pnewpath);
	if (strcmp(path_directory, "/")) {
		path_dir = hash_table_get(files, path_directory);
		if (!path_dir)
			return FSROOT_E_NOTEXISTS;
	}
	if (strcmp(newpath_directory, "/")) {
		newpath_dir = hash_table_get(files, newpath_directory);
		if (!newpath_dir)
			return FSROOT_E_NOTEXISTS;
	}

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
	mm_free(file->name);
	file->name = new_basename;
	__fsroot_create_file(newpath_dir, file);
	hash_table_put(files, pnewpath, file);

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
int fsroot_opendir(const char *path, struct fsroot_file **outdir)
{
	int retval;
	struct fsroot_directory *dir;

	if (!path || !outdir)
		return FSROOT_E_BADARGS;

	dir = hash_table_get(files, path);
	if (dir && S_ISDIR(dir->mode)) {
		*outdir = (struct fsroot_file *) dir;
//		memcpy(outdir, dir, sizeof(struct fsroot_file));
//		outdir->name = dir->name;
//		outdir->uid = dir->uid;
//		outdir->gid = dir->gid;
//		outdir->mode = dir->mode;
		retval = FSROOT_OK;
	} else {
		retval = FSROOT_E_NOTEXISTS;
	}

	return retval;
}

int fsroot_readdir(off_t offset, struct fsroot_file *directory, struct fsroot_file *file)
{
	int retval;
	struct fsroot_directory *dir;

	if (!directory || !file)
		return FSROOT_E_BADARGS;

	dir = (struct fsroot_directory *) directory;

	if (offset >= dir->num_entries) {
		retval = FSROOT_OK;
	} else {
		struct fsroot_file *f = dir->entries[offset];
		file->name = f->name;
		file->mode = f->mode;
		file->uid = f->uid;
		file->gid = f->gid;
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
	struct fsroot_directory *dir_foo = (struct fsroot_directory *) fsroot_create_file(NULL, "foo", 1000, 1000, S_IFDIR),
		*dir_bar = (struct fsroot_directory *) fsroot_create_file(NULL, "bar", 1000, 1000, S_IFDIR),
		*dir_baz = (struct fsroot_directory *) fsroot_create_file(dir_bar, "baz", 1000, 1000, S_IFDIR);

	hash_table_put(files, "/foo", dir_foo);
	hash_table_put(files, "/bar", dir_bar);
	hash_table_put(files, "/bar/baz", dir_baz);

	struct fsroot_file *test = fsroot_create_file(NULL, "test", 1000, 1000, S_IFREG),
//		*test_foo = fsroot_create_file(dir_foo, "test", 1000, 1000, 0),
		*test_baz = fsroot_create_file(dir_baz, "test", 1000, 1000, S_IFREG);

	hash_table_put(files, "/test", test);
//	hash_table_put(files, "/foo/test", test_foo);
	hash_table_put(files, "/bar/baz/test", test_baz);

	struct fsroot_file *dir, file;
	int retval = fsroot_opendir("/bar/baz", &dir);
	if (retval == FSROOT_E_NOTEXISTS)
		goto end;

	fsroot_readdir(0, dir, &file);
	fsroot_readdir(1, dir, &file);

//	char linkpath[PATH_MAX];
//	fsroot_symlink("/TEST", "/test", 1000, 1000);
//	fsroot_symlink("/TEST", "/test2", 1000, 1000);
//	fsroot_readlink("/TEST", linkpath, sizeof(linkpath));
//
//	fsroot_symlink("/bar/baz/TEST", "/test", 1000, 1000);
//	fsroot_readlink("/bar/baz/TEST", linkpath, sizeof(linkpath));

	fsroot_mkdir("/foo/bar", 1000, 1000);
	fsroot_mkdir("/foo/bar", 1000, 1000);
	fsroot_rmdir("/foo/bar");

	fsroot_chmod("/bar/baz/test", 0777);
	fsroot_chown("/bar/baz/test", 2000, 1000);

	fsroot_rename("/bar/baz/test", "/bar/baz/TEST");
	fsroot_rename("/bar/baz/TEST", "/foo/test");
	fsroot_rename("/test", "/foo/TEST");
	fsroot_rename("/foo/test", "/test");

end:
	hash_table_destroy(files);
	return 0;
}
