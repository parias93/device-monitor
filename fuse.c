/*
 * fuse.c
 *
 *  Created on: 21 Oct 2016
 *      Author: Ander Juaristi
 *
 *  Unsupported operations:
 *  	- readlink
 *  	- symlink
 *  	- link
 *  	- statfs
 *  	- flush
 *  	- release ??? -> we have to open+close with every open()
 *  	- fsync
 *  	- setxattr
 *  	- getxattr
 *  	- listxattr
 *  	- removexattr
 *  	- releasedir ???
 *  	- fsyncdir
 *  	- destroy
 *  	- lock
 *  	- utimens
 *  	- bmap
 *  	- ioctl
 *  	- poll
 *  	- write_buf
 *  	- read_buf
 *  	- flock
 *  	- fallocate
 *  	- create
 */
#define FUSE_USE_VERSION 30
#include <linux/limits.h>
#include <unistd.h>	/* rmdir(2), stat(2), unlink(2), chown(2) */
#include <sys/stat.h>	/* mkdir(2), chmod(2) */
#include <stdio.h>	/* rename(2) */
#include <dirent.h>
#include <stddef.h>	/* offsetof() macro */
#include <errno.h>
#include <fuse.h>
#include <fuse_lowlevel.h>

int dm_fullpath(const char *in, char *out, size_t outlen);

static void *dm_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	printf("DroneFS device monitor. Written by Ander Juaristi.\n");
	return NULL;
}

/*
 * Get file attributes.
 */
static int dm_fuse_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;
	if (!st)
		return -EFAULT;

	return lstat(fullpath, st);
}

/*
 * Create a file node.
 * This is called for all non-directory and non-symlink nodes.
 * If the create() method is defined, then for regular files that will be called instead.
 */
static int dm_fuse_mknod(const char *path, mode_t mode, dev_t dev)
{
	int retval = 0;
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;
	if (!S_ISREG(mode))
		return -EACCES;

	retval = open(fullpath, O_CREAT | O_EXCL | O_WRONLY, mode);
	if (retval >= 0)
		close(retval);

	return retval;
}

/*
 * Create a directory.
 */
static int dm_fuse_mkdir(const char *path, mode_t mode)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return mkdir(fullpath, mode);
}

/*
 * Remove a file.
 */
static int dm_fuse_unlink(const char *path)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return unlink(fullpath);
}

/*
 * Remove a directory.
 */
static int dm_fuse_rmdir(const char *path)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return rmdir(fullpath);
}

/*
 * Rename a file.
 */
static int dm_fuse_rename(const char *path, const char *newpath, unsigned int foo)
{
	char fullpath[PATH_MAX], full_newpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;
	if (!newpath || !dm_fullpath(newpath, full_newpath, sizeof(full_newpath)))
		return -EFAULT;

	return rename(fullpath, full_newpath);
}

/*
 * Change the permission bits of a file.
 */
static int dm_fuse_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return chmod(fullpath, mode);
}

/*
 * Change the owner and group of a file.
 */
static int dm_fuse_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return chown(fullpath, uid, gid);
}

/*
 * Change the size of a file.
 */
static int dm_fuse_truncate(const char *path, off_t newsize, struct fuse_file_info *fi)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return truncate(fullpath, newsize);
}

/*
 * Open a file.
 * No creation (O_CREAT, O_EXCL) and by default also no truncation (O_TRUNC) flags
 * will be passed.
 * Unless the 'default_permissions' mount option is given, open should check
 * whether the operation is permitted for the given flags.
 */
static int dm_fuse_open(const char *path, struct fuse_file_info *fi)
{
	int fd;
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	fd = open(fullpath, fi->flags);
	if (fd < 0)
		return -1;

	fi->fh = fd;
	return 0;
}

/*
 * Read data from an open file.
 * Should return exactly the number of bytes requested except on EOF or error,
 * otherwise the rest of the data will be substituted with zeroes.
 * An exception to this is when the 'direct_io' mount option is specified,
 * in which case the return value of the read system call will reflect
 * the return value of this operation.
 */
static int dm_fuse_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	return pread(fi->fh, buf, size, offset);
}

/*
 * Write data to an open file.
 * Write should return exactly the number of bytes requested except on error.
 * An exception to this is when the 'direct_io' mount option is specified (see read operation).
 */
static int dm_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	return pwrite(fi->fh, buf, size, offset);
}

/*
 * Open directory.
 * Unless the 'default_permissions' mount option is given,
 * this method should check if opendir is permitted for this directory.
 * Optionally opendir may also return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to readdir, closedir and fsyncdir.
 */
static int dm_fuse_opendir(const char *path, struct fuse_file_info *fi)
{
	DIR *dp;
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;
	if (!fi)
		return -EFAULT;

	dp = opendir(fullpath);
	fi->fh = (uintptr_t) dp;

	return (dp == NULL ? -1 : 0);
}

/*
 * Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 	1) The readdir implementation ignores the offset parameter,
 * 	and passes zero to the filler function's offset. The filler function
 * 	will not return '1' (unless an error happens), so the whole directory is
 * 	read in a single readdir operation.
 *
 * 	2) The readdir implementation keeps track of the offsets of the directory entries.
 * 	It uses the offset parameter and always passes non-zero offset to the filler function.
 * 	When the buffer is full (or an error happens) the filler function will return '1'.
 */
static int dm_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	DIR *dp;
	struct dirent *de;
	int initial_errno = errno;

	dp = (DIR *) (uintptr_t) fi->fh;
	de = readdir(dp);
	if (de == 0)
		return -1;

	do {
		if (filler(buf, de->d_name, NULL, 0,
				(flags == FUSE_READDIR_PLUS ? FUSE_FILL_DIR_PLUS : 0))
				!= 0)
			return -ENOMEM;
	} while ((de = readdir(dp)) != NULL);

	return (errno == initial_errno ? 0 : -1);
}

/*
 * Check file access permissions.
 * This will be called for access(2), unless the 'default_permissions'
 * mount option is given.
 */
static int dm_fuse_access(const char *path, int mask)
{
	char fullpath[PATH_MAX];

	if (!path || !dm_fullpath(path, fullpath, sizeof(fullpath)))
		return -EFAULT;

	return access(fullpath, mask);
}

void print_help()
{
	printf("<mount point> <root dir>\n");
}

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_operations dm_operations = {
		.init           = dm_fuse_init,
		.getattr	= dm_fuse_getattr,
		.mknod		= dm_fuse_mknod,
		.mkdir		= dm_fuse_mkdir,
		.unlink		= dm_fuse_unlink,
		.rmdir		= dm_fuse_rmdir,
		.rename		= dm_fuse_rename,
		.chmod		= dm_fuse_chmod,
		.chown		= dm_fuse_chown,
		.truncate	= dm_fuse_truncate,
		.open		= dm_fuse_open,
		.read		= dm_fuse_read,
		.write		= dm_fuse_write,
		.opendir	= dm_fuse_opendir,
		.readdir	= dm_fuse_readdir,
		.access		= dm_fuse_access
	};
	struct options {
		int show_help;
	} options = {0};
	const struct fuse_opt opts[] = {
		{"-h", offsetof(struct options, show_help), 1},
		{"--help", offsetof(struct options, show_help), 1},
		FUSE_OPT_END
	};

	if (fuse_opt_parse(&args, &options, opts, NULL) == -1)
		return 1;

	if (options.show_help) {
		print_help();
		return 0;
	}

	return fuse_main(args.argc, args.argv, &dm_operations, NULL);
}
