#include "Headers.hpp"

#ifdef __APPLE__

#ifdef __APPLE__
#include <AvailabilityMacros.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
#error "This file system requires Leopard and above."
#endif

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
#define HAVE_FSETATTR_X 0
#else
#define HAVE_FSETATTR_X 1
#endif
#endif

#if defined(_POSIX_C_SOURCE)
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
#endif

#define G_PREFIX                       "org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX ".apple.system.Security"
#define A_PREFIX                       "com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX ".apple.system.Security"
#define XATTR_APPLE_PREFIX             "com.apple."

struct loopback {
	int case_insensitive;
};

static struct loopback loopback;

static int
loopback_getattr(const char *path, struct stat *stbuf)
{
    int res;

    res = lstat(path, stbuf);

#if FUSE_VERSION >= 29
    /*
     * The optimal I/O size can be set on a per-file basis. Setting st_blksize
     * to zero will cause the kernel extension to fall back on the global I/O
     * size which can be specified at mount-time (option iosize).
     */
    stbuf->st_blksize = 0;
#endif

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_fgetattr(const char *path, struct stat *stbuf,
                  struct fuse_file_info *fi)
{
    int res;

    (void)path;

    res = fstat(fi->fh, stbuf);

#if FUSE_VERSION >= 29
    // Fall back to global I/O size. See loopback_getattr().
    stbuf->st_blksize = 0;
#endif

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_readlink(const char *path, char *buf, size_t size)
{
    int res;

    res = readlink(path, buf, size - 1);
    if (res == -1) {
        return -errno;
    }

    buf[res] = '\0';

    return 0;
}

struct loopback_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static int
loopback_opendir(const char *path, struct fuse_file_info *fi)
{
    int res;

    struct loopback_dirp *d = (struct loopback_dirp *)malloc(sizeof(struct loopback_dirp));
    if (d == NULL) {
        return -ENOMEM;
    }

    d->dp = opendir(path);
    if (d->dp == NULL) {
        res = -errno;
        free(d);
        return res;
    }

    d->offset = 0;
    d->entry = NULL;

    fi->fh = (unsigned long)d;

    return 0;
}

static inline struct loopback_dirp *
get_dirp(struct fuse_file_info *fi)
{
    return (struct loopback_dirp *)(uintptr_t)fi->fh;
}

static int
loopback_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi)
{
    struct loopback_dirp *d = get_dirp(fi);

    (void)path;

    if (offset != d->offset) {
        seekdir(d->dp, offset);
        d->entry = NULL;
        d->offset = offset;
    }

    while (1) {
        struct stat st;
        off_t nextoff;

        if (!d->entry) {
            d->entry = readdir(d->dp);
            if (!d->entry) {
                break;
            }
        }

        memset(&st, 0, sizeof(st));
        st.st_ino = d->entry->d_ino;
        st.st_mode = d->entry->d_type << 12;
        nextoff = telldir(d->dp);
        if (filler(buf, d->entry->d_name, &st, nextoff)) {
            break;
        }

        d->entry = NULL;
        d->offset = nextoff;
    }

    return 0;
}

static int
loopback_releasedir(const char *path, struct fuse_file_info *fi)
{
    struct loopback_dirp *d = get_dirp(fi);

    (void)path;

    closedir(d->dp);
    free(d);

    return 0;
}

static int
loopback_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    if (S_ISFIFO(mode)) {
        res = mkfifo(path, mode);
    } else {
        res = mknod(path, mode, rdev);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_mkdir(const char *path, mode_t mode)
{
    int res;

    res = mkdir(path, mode);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_unlink(const char *path)
{
    int res;

    res = unlink(path);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_rmdir(const char *path)
{
    int res;

    res = rmdir(path);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_symlink(const char *from, const char *to)
{
    int res;

    res = symlink(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_rename(const char *from, const char *to)
{
    int res;

    res = rename(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_exchange(const char *path1, const char *path2, unsigned long options)
{
    int res;

    res = exchangedata(path1, path2, options);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_link(const char *from, const char *to)
{
    int res;

    res = link(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

#if HAVE_FSETATTR_X

static int
loopback_fsetattr_x(const char *path, struct setattr_x *attr,
                    struct fuse_file_info *fi)
{
    int res;
    uid_t uid = -1;
    gid_t gid = -1;

    if (SETATTR_WANTS_MODE(attr)) {
        res = fchmod(fi->fh, attr->mode);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_UID(attr)) {
        uid = attr->uid;
    }

    if (SETATTR_WANTS_GID(attr)) {
        gid = attr->gid;
    }

    if ((uid != -1) || (gid != -1)) {
        res = fchown(fi->fh, uid, gid);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_SIZE(attr)) {
        res = ftruncate(fi->fh, attr->size);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_MODTIME(attr)) {
        struct timeval tv[2];
        if (!SETATTR_WANTS_ACCTIME(attr)) {
            gettimeofday(&tv[0], NULL);
        } else {
            tv[0].tv_sec = attr->acctime.tv_sec;
            tv[0].tv_usec = attr->acctime.tv_nsec / 1000;
        }
        tv[1].tv_sec = attr->modtime.tv_sec;
        tv[1].tv_usec = attr->modtime.tv_nsec / 1000;
        res = futimes(fi->fh, tv);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CRTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CRTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->crtime,
                           sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CHGTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CHGTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->chgtime,
                           sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_BKUPTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_BKUPTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->bkuptime,
                           sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_FLAGS(attr)) {
        res = fchflags(fi->fh, attr->flags);
        if (res == -1) {
            return -errno;
        }
    }

    return 0;
}

#endif /* HAVE_FSETATTR_X */

static int
loopback_setattr_x(const char *path, struct setattr_x *attr)
{
    int res;
    uid_t uid = -1;
    gid_t gid = -1;

    if (SETATTR_WANTS_MODE(attr)) {
        res = lchmod(path, attr->mode);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_UID(attr)) {
        uid = attr->uid;
    }

    if (SETATTR_WANTS_GID(attr)) {
        gid = attr->gid;
    }

    if ((uid != -1) || (gid != -1)) {
        res = lchown(path, uid, gid);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_SIZE(attr)) {
        res = truncate(path, attr->size);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_MODTIME(attr)) {
        struct timeval tv[2];
        if (!SETATTR_WANTS_ACCTIME(attr)) {
            gettimeofday(&tv[0], NULL);
        } else {
            tv[0].tv_sec = attr->acctime.tv_sec;
            tv[0].tv_usec = attr->acctime.tv_nsec / 1000;
        }
        tv[1].tv_sec = attr->modtime.tv_sec;
        tv[1].tv_usec = attr->modtime.tv_nsec / 1000;
        res = lutimes(path, tv);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CRTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CRTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->crtime,
                          sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CHGTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CHGTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->chgtime,
                          sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_BKUPTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_BKUPTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->bkuptime,
                          sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_FLAGS(attr)) {
        res = lchflags(path, attr->flags);
        if (res == -1) {
            return -errno;
        }
    }

    return 0;
}

static int
loopback_getxtimes(const char *path, struct timespec *bkuptime,
                   struct timespec *crtime)
{
    int res = 0;
    struct attrlist attributes;

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved    = 0;
    attributes.commonattr  = 0;
    attributes.dirattr     = 0;
    attributes.fileattr    = 0;
    attributes.forkattr    = 0;
    attributes.volattr     = 0;

    struct xtimeattrbuf {
        uint32_t size;
        struct timespec xtime;
    } __attribute__ ((packed));

    struct xtimeattrbuf buf;

    attributes.commonattr = ATTR_CMN_BKUPTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0) {
        (void)memcpy(bkuptime, &(buf.xtime), sizeof(struct timespec));
    } else {
        (void)memset(bkuptime, 0, sizeof(struct timespec));
    }

    attributes.commonattr = ATTR_CMN_CRTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0) {
        (void)memcpy(crtime, &(buf.xtime), sizeof(struct timespec));
    } else {
        (void)memset(crtime, 0, sizeof(struct timespec));
    }

    return 0;
}

static int
loopback_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int fd;

    fd = open(path, fi->flags, mode);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = fd;
    return 0;
}

static int
loopback_open(const char *path, struct fuse_file_info *fi)
{
    int fd;

    fd = open(path, fi->flags);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = fd;
    return 0;
}

static int
loopback_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    int res;

    (void)path;
    res = pread(fi->fh, buf, size, offset);
    if (res == -1) {
        res = -errno;
    }

    return res;
}

static int
loopback_write(const char *path, const char *buf, size_t size,
               off_t offset, struct fuse_file_info *fi)
{
    int res;

    (void)path;

    res = pwrite(fi->fh, buf, size, offset);
    if (res == -1) {
        res = -errno;
    }

    return res;
}

static int
loopback_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    res = statvfs(path, stbuf);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_flush(const char *path, struct fuse_file_info *fi)
{
    int res;

    (void)path;

    res = close(dup(fi->fh));
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;

    close(fi->fh);

    return 0;
}

static int
loopback_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int res;

    (void)path;

    (void)isdatasync;

    res = fsync(fi->fh);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_setxattr(const char *path, const char *name, const char *value,
                  size_t size, int flags, uint32_t position)
{
    int res;

    if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
        flags &= ~(XATTR_NOSECURITY);
    }

    if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {

        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = setxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);

    } else {
        res = setxattr(path, name, value, size, position, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int
loopback_getxattr(const char *path, const char *name, char *value, size_t size,
                  uint32_t position)
{
    int res;

    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {

        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = getxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);

    } else {
        res = getxattr(path, name, value, size, position, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return res;
}

static int
loopback_listxattr(const char *path, char *list, size_t size)
{
    ssize_t res = listxattr(path, list, size, XATTR_NOFOLLOW);
    if (res > 0) {
        if (list) {
            size_t len = 0;
            char *curr = list;
            do {
                size_t thislen = strlen(curr) + 1;
                if (strcmp(curr, G_KAUTH_FILESEC_XATTR) == 0) {
                    memmove(curr, curr + thislen, res - len - thislen);
                    res -= thislen;
                    break;
                }
                curr += thislen;
                len += thislen;
            } while (len < res);
        } else {
            /*
            ssize_t res2 = getxattr(path, G_KAUTH_FILESEC_XATTR, NULL, 0, 0,
                                    XATTR_NOFOLLOW);
            if (res2 >= 0) {
                res -= sizeof(G_KAUTH_FILESEC_XATTR);
            }
            */
        }
    }

    if (res == -1) {
        return -errno;
    }

    return res;
}

static int
loopback_removexattr(const char *path, const char *name)
{
    int res;

    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {

        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = removexattr(path, new_name, XATTR_NOFOLLOW);

    } else {
        res = removexattr(path, name, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

#if FUSE_VERSION >= 29

static int
loopback_fallocate(const char *path, int mode, off_t offset, off_t length,
                   struct fuse_file_info *fi)
{
    fstore_t fstore;

    if (!(mode & PREALLOCATE)) {
        return -ENOTSUP;
    }

    fstore.fst_flags = 0;
    if (mode & ALLOCATECONTIG) {
        fstore.fst_flags |= F_ALLOCATECONTIG;
    }
    if (mode & ALLOCATEALL) {
        fstore.fst_flags |= F_ALLOCATEALL;
    }

    if (mode & ALLOCATEFROMPEOF) {
        fstore.fst_posmode = F_PEOFPOSMODE;
    } else if (mode & ALLOCATEFROMVOL) {
        fstore.fst_posmode = F_VOLPOSMODE;
    }

    fstore.fst_offset = offset;
    fstore.fst_length = length;

    if (fcntl(fi->fh, F_PREALLOCATE, &fstore) == -1) {
        return -errno;
    } else {
        return 0;
    }
}

#endif /* FUSE_VERSION >= 29 */

static int
loopback_setvolname(const char *name)
{
    return 0;
}

void *
loopback_init(struct fuse_conn_info *conn)
{
    FUSE_ENABLE_SETVOLNAME(conn);
    FUSE_ENABLE_XTIMES(conn);

#ifdef FUSE_ENABLE_CASE_INSENSITIVE
    if (loopback.case_insensitive) {
        FUSE_ENABLE_CASE_INSENSITIVE(conn);
    }
#endif

    return NULL;
}

void
loopback_destroy(void *userdata)
{
    /* nothing */
}

static struct fuse_operations loopback_oper = {
    .init        = loopback_init,
    .destroy     = loopback_destroy,
    .getattr     = loopback_getattr,
    .fgetattr    = loopback_fgetattr,
/*  .access      = loopback_access, */
    .readlink    = loopback_readlink,
    .opendir     = loopback_opendir,
    .readdir     = loopback_readdir,
    .releasedir  = loopback_releasedir,
    .mknod       = loopback_mknod,
    .mkdir       = loopback_mkdir,
    .symlink     = loopback_symlink,
    .unlink      = loopback_unlink,
    .rmdir       = loopback_rmdir,
    .rename      = loopback_rename,
    .link        = loopback_link,
    .create      = loopback_create,
    .open        = loopback_open,
    .read        = loopback_read,
    .write       = loopback_write,
    .statfs      = loopback_statfs,
    .flush       = loopback_flush,
    .release     = loopback_release,
    .fsync       = loopback_fsync,
    .setxattr    = loopback_setxattr,
    .getxattr    = loopback_getxattr,
    .listxattr   = loopback_listxattr,
    .removexattr = loopback_removexattr,
    .exchange    = loopback_exchange,
    .getxtimes   = loopback_getxtimes,
    .setattr_x   = loopback_setattr_x,
#if HAVE_FSETATTR_X
    .fsetattr_x  = loopback_fsetattr_x,
#endif
#if FUSE_VERSION >= 29
    .fallocate   = loopback_fallocate,
#endif
    .setvolname  = loopback_setvolname,

#if FUSE_VERSION >= 29
#if HAVE_FSETATTR_X
    .flag_nullpath_ok = 1,
    .flag_nopath = 1,
#else
    .flag_nullpath_ok = 0,
    .flag_nopath = 0,
#endif
#endif /* FUSE_VERSION >= 29 */
};

static const struct fuse_opt loopback_opts[] = {
	{ "case_insensitive", offsetof(struct loopback, case_insensitive), 1 },
	FUSE_OPT_END
};

int
main(int argc, char *argv[])
{
    int res = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    loopback.case_insensitive = 0;
    if (fuse_opt_parse(&args, &loopback, loopback_opts, NULL) == -1) {
		exit(1);
    }

    umask(0);
    res = fuse_main(args.argc, args.argv, &loopback_oper, NULL);

    fuse_opt_free_args(&args);
    return res;
}

#else

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct xmp_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int xmp_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	struct xmp_dirp *d = (struct xmp_dirp *)malloc(sizeof(struct xmp_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(path);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct xmp_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct xmp_dirp *) (uintptr_t) fi->fh;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int xmp_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_read_buf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;

	src = (struct fuse_bufvec *)malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_write_buf(const char *path, struct fuse_bufvec *buf,
		     off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

	(void) path;

	dst.buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
	dst.buf[0].fd = fi->fh;
	dst.buf[0].pos = offset;

	return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif

static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}

static int xmp_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void) path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}

int main(int argc, char *argv[])
{
	umask(0);
    fuse_operations xmp_oper;
    memset(&xmp_oper, 0, sizeof(fuse_operations));
	xmp_oper.getattr	= xmp_getattr;
	xmp_oper.fgetattr	= xmp_fgetattr;
	xmp_oper.access		= xmp_access;
	xmp_oper.readlink	= xmp_readlink;
	xmp_oper.opendir	= xmp_opendir;
	xmp_oper.readdir	= xmp_readdir;
	xmp_oper.releasedir	= xmp_releasedir;
	xmp_oper.mknod		= xmp_mknod;
	xmp_oper.mkdir		= xmp_mkdir;
	xmp_oper.symlink	= xmp_symlink;
	xmp_oper.unlink		= xmp_unlink;
	xmp_oper.rmdir		= xmp_rmdir;
	xmp_oper.rename		= xmp_rename;
	xmp_oper.link		= xmp_link;
	xmp_oper.chmod		= xmp_chmod;
	xmp_oper.chown		= xmp_chown;
	xmp_oper.truncate	= xmp_truncate;
	xmp_oper.ftruncate	= xmp_ftruncate;
#ifdef HAVE_UTIMENSAT
	xmp_oper.utimens	= xmp_utimens;
#endif
	xmp_oper.create		= xmp_create;
	xmp_oper.open		= xmp_open;
	xmp_oper.read		= xmp_read;
	xmp_oper.read_buf	= xmp_read_buf;
	xmp_oper.write		= xmp_write;
	xmp_oper.write_buf	= xmp_write_buf;
	xmp_oper.statfs		= xmp_statfs;
	xmp_oper.flush		= xmp_flush;
	xmp_oper.release	= xmp_release;
	xmp_oper.fsync		= xmp_fsync;
#ifdef HAVE_POSIX_FALLOCATE
	xmp_oper.fallocate	= xmp_fallocate;
#endif
	xmp_oper.setxattr	= xmp_setxattr;
	xmp_oper.getxattr	= xmp_getxattr;
	xmp_oper.listxattr	= xmp_listxattr;
	xmp_oper.removexattr	= xmp_removexattr;
	xmp_oper.lock		= xmp_lock;
	xmp_oper.flock		= xmp_flock;

	xmp_oper.flag_nullpath_ok = 1;
#if HAVE_UTIMENSAT
	xmp_oper.flag_utime_omit_ok = 1;
#endif
	return fuse_main(argc, argv, &xmp_oper, NULL);
}

#endif
