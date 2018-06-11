#include "Headers.hpp"

class FdInfo {
 public:
  string path;

  FdInfo(const string &_path) : path(_path) {}
};

struct loopback_dirp {
  DIR *dp;
  struct dirent *entry;
  off_t offset;
};

unordered_map<int64_t, FdInfo> fdMap;

static int loopback_getattr(const char *path, struct stat *stbuf) {
  int res;

  res = lstat(path, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_fgetattr(const char *path, struct stat *stbuf,
                             struct fuse_file_info *fi) {
  auto it = fdMap.find(fi->fh);
  if (it == fdMap.end()) {
    errno = EBADF;
    return -errno;
  }
  return loopback_getattr(it->second.path.c_str(), stbuf);
}

static int loopback_access(const char *path, int mask) {
  int res;

  res = access(path, mask);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_readlink(const char *path, char *buf, size_t size) {
  int res;

  res = readlink(path, buf, size - 1);
  if (res == -1) return -errno;

  buf[res] = '\0';
  return 0;
}

static int loopback_opendir(const char *path, struct fuse_file_info *fi) {
  int res;
  struct loopback_dirp *d =
      (struct loopback_dirp *)malloc(sizeof(struct loopback_dirp));
  if (d == NULL) return -ENOMEM;

  d->dp = opendir(path);
  if (d->dp == NULL) {
    res = -errno;
    free(d);
    return res;
  }
  d->offset = 0;
  d->entry = NULL;

  fi->fh = (unsigned long)d;
  fdMap.insert(make_pair((unsigned long)d, FdInfo(string(path))));
  return 0;
}

static inline struct loopback_dirp *get_dirp(struct fuse_file_info *fi) {
  return (struct loopback_dirp *)(uintptr_t)fi->fh;
}

static int loopback_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi) {
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
      if (!d->entry) break;
    }

    memset(&st, 0, sizeof(st));
    st.st_ino = d->entry->d_ino;
    st.st_mode = d->entry->d_type << 12;
    nextoff = telldir(d->dp);
    if (filler(buf, d->entry->d_name, &st, nextoff)) break;

    d->entry = NULL;
    d->offset = nextoff;
  }

  return 0;
}

static int loopback_releasedir(const char *path, struct fuse_file_info *fi) {
  struct loopback_dirp *d = get_dirp(fi);
  (void)path;
  closedir(d->dp);
  free(d);
  auto it = fdMap.find(((unsigned long)d));
  if (it == fdMap.end()) {
    LOG(FATAL) << "Tried to close a dir that doesn't exist";
  }
  fdMap.erase(it);
  return 0;
}

static int loopback_mknod(const char *path, mode_t mode, dev_t rdev) {
  int res;

  if (S_ISFIFO(mode))
    res = mkfifo(path, mode);
  else
    res = mknod(path, mode, rdev);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_mkdir(const char *path, mode_t mode) {
  int res;

  res = mkdir(path, mode);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_unlink(const char *path) {
  int res;

  res = unlink(path);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_rmdir(const char *path) {
  int res;

  res = rmdir(path);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_symlink(const char *from, const char *to) {
  int res;

  res = symlink(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_rename(const char *from, const char *to) {
  int res;

  res = rename(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_link(const char *from, const char *to) {
  LOG(FATAL) << "Not implemented";
  int res;

  res = link(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_chmod(const char *path, mode_t mode) {
  int res;

  res = chmod(path, mode);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_chown(const char *path, uid_t uid, gid_t gid) {
  int res;

  res = lchown(path, uid, gid);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_truncate(const char *path, off_t size) {
  int res;

  res = truncate(path, size);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_ftruncate(const char *path, off_t size,
                              struct fuse_file_info *fi) {
  int res;

  (void)path;

  res = ftruncate(fi->fh, size);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_create(const char *path, mode_t mode,
                           struct fuse_file_info *fi) {
  int fd;

  fd = open(path, fi->flags, mode);
  if (fd == -1) return -errno;

  fi->fh = fd;
  return 0;
}

static int loopback_open(const char *path, struct fuse_file_info *fi) {
  int fd;

  fd = open(path, fi->flags);
  if (fd == -1) return -errno;

  fi->fh = fd;
  fdMap.insert(make_pair((int64_t)fd,FdInfo(string(path))));
  return 0;
}

static int loopback_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
  int res;

  (void)path;
  res = pread(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int loopback_read_buf(const char *path, struct fuse_bufvec **bufp,
                             size_t size, off_t offset,
                             struct fuse_file_info *fi) {
  struct fuse_bufvec *src;

  (void)path;

  src = (struct fuse_bufvec *)malloc(sizeof(struct fuse_bufvec));
  if (src == NULL) return -ENOMEM;

  *src = FUSE_BUFVEC_INIT(size);

  src->buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
  src->buf[0].fd = fi->fh;
  src->buf[0].pos = offset;

  *bufp = src;

  return 0;
}

static int loopback_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi) {
  int res;

  (void)path;
  res = pwrite(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int loopback_write_buf(const char *path, struct fuse_bufvec *buf,
                              off_t offset, struct fuse_file_info *fi) {
  struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

  (void)path;

  dst.buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
  dst.buf[0].fd = fi->fh;
  dst.buf[0].pos = offset;

  return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

static int loopback_statfs(const char *path, struct statvfs *stbuf) {
  int res;

  res = statvfs(path, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_flush(const char *path, struct fuse_file_info *fi) {
  int res;

  (void)path;
  /* This is called from every close on an open file, so call the
     close on the underlying filesystem.	But since flush may be
     called multiple times for an open file, this must not really
     close the file.  This is important if used on a network
     filesystem like NFS which flush the data/metadata on close() */
  res = close(dup(fi->fh));
  if (res == -1) return -errno;

  return 0;
}

static int loopback_release(const char *path, struct fuse_file_info *fi) {
  (void)path;
  close(fi->fh);
  auto it = fdMap.find(fi->fh);
  if (it == fdMap.end()) {
    LOG(FATAL) << "Tried to close an fd that doesn't exist";
  }
  fdMap.erase(it);

  return 0;
}

static int loopback_fsync(const char *path, int isdatasync,
                          struct fuse_file_info *fi) {
  int res;
  (void)path;

  if (isdatasync) {
#ifdef F_FULLFSYNC
    /* this is a Mac OS X system which does not implement fdatasync as such */
    res = fcntl(fi->fh, F_FULLFSYNC);
#else
    res = fdatasync(fi->fh);
#endif
  } else
    res = fsync(fi->fh);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_flock(const char *path, struct fuse_file_info *fi, int op) {
  int res;
  (void)path;

  res = flock(fi->fh, op);
  if (res == -1) return -errno;

  return 0;
}

static int loopback_utimens(const char *path, const struct timespec ts[2]) {
  int res;

  /* don't use utime/utimes since they follow symlinks */
  res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
  if (res == -1) return -errno;

  return 0;
}

struct loopback {};

static struct loopback loopback;

#ifdef __APPLE__

#include <AvailabilityMacros.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
#error "This file system requires Snow Leopard and above."
#endif

#if defined(_POSIX_C_SOURCE)
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
#endif

#define G_PREFIX "org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX ".apple.system.Security"
#define A_PREFIX "com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX ".apple.system.Security"
#define XATTR_APPLE_PREFIX "com.apple."

static int loopback_exchange(const char *path1, const char *path2,
                             unsigned long options) {
  int res;

  res = exchangedata(path1, path2, options);
  if (res == -1) {
    return -errno;
  }

  return 0;
}

static int loopback_fsetattr_x(const char *path, struct setattr_x *attr,
                               struct fuse_file_info *fi) {
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

static int loopback_setattr_x(const char *path, struct setattr_x *attr) {
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

    res = setattrlist(path, &attributes, &attr->crtime, sizeof(struct timespec),
                      FSOPT_NOFOLLOW);

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

static int loopback_getxtimes(const char *path, struct timespec *bkuptime,
                              struct timespec *crtime) {
  int res = 0;
  struct attrlist attributes;

  attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
  attributes.reserved = 0;
  attributes.commonattr = 0;
  attributes.dirattr = 0;
  attributes.fileattr = 0;
  attributes.forkattr = 0;
  attributes.volattr = 0;

  struct xtimeattrbuf {
    uint32_t size;
    struct timespec xtime;
  } __attribute__((packed));

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

static int loopback_setxattr(const char *path, const char *name,
                             const char *value, size_t size, int flags,
                             uint32_t position) {
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

static int loopback_getxattr(const char *path, const char *name, char *value,
                             size_t size, uint32_t position) {
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

static int loopback_listxattr(const char *path, char *list, size_t size) {
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

static int loopback_removexattr(const char *path, const char *name) {
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

static int loopback_setvolname(const char *name) { return 0; }

void *loopback_init(struct fuse_conn_info *conn) {
  FUSE_ENABLE_SETVOLNAME(conn);
  FUSE_ENABLE_XTIMES(conn);

  return NULL;
}

void loopback_destroy(void *userdata) { /* nothing */
}

#else

static int loopback_setxattr(const char *path, const char *name,
                             const char *value, size_t size, int flags) {
  int res = lsetxattr(path, name, value, size, flags);
  if (res == -1) return -errno;
  return 0;
}

static int loopback_getxattr(const char *path, const char *name, char *value,
                             size_t size) {
  int res = lgetxattr(path, name, value, size);
  if (res == -1) return -errno;
  return res;
}

static int loopback_listxattr(const char *path, char *list, size_t size) {
  int res = llistxattr(path, list, size);
  if (res == -1) return -errno;
  return res;
}

static int loopback_removexattr(const char *path, const char *name) {
  int res = lremovexattr(path, name);
  if (res == -1) return -errno;
  return 0;
}

static int loopback_lock(const char *path, struct fuse_file_info *fi, int cmd,
                         struct flock *lock) {
  (void)path;

  return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
                     sizeof(fi->lock_owner));
}

#endif

static const struct fuse_opt loopback_opts[] = {
    // { "case_insensitive", offsetof(struct loopback, case_insensitive), 1 },
    FUSE_OPT_END};

int main(int argc, char *argv[]) {
  int res = 0;
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  if (fuse_opt_parse(&args, &loopback, loopback_opts, NULL) == -1) {
    exit(1);
  }

  umask(0);
  fuse_operations loopback_oper;
  memset(&loopback_oper, 0, sizeof(fuse_operations));
  loopback_oper.getattr = loopback_getattr;
  loopback_oper.fgetattr = loopback_fgetattr;
  loopback_oper.access = loopback_access;
  loopback_oper.readlink = loopback_readlink;
  loopback_oper.opendir = loopback_opendir;
  loopback_oper.readdir = loopback_readdir;
  loopback_oper.releasedir = loopback_releasedir;
  loopback_oper.mknod = loopback_mknod;
  loopback_oper.mkdir = loopback_mkdir;
  loopback_oper.symlink = loopback_symlink;
  loopback_oper.unlink = loopback_unlink;
  loopback_oper.rmdir = loopback_rmdir;
  loopback_oper.rename = loopback_rename;
  loopback_oper.link = loopback_link;
  loopback_oper.chmod = loopback_chmod;
  loopback_oper.chown = loopback_chown;
  loopback_oper.truncate = loopback_truncate;
  loopback_oper.ftruncate = loopback_ftruncate;
  loopback_oper.utimens = loopback_utimens;
  loopback_oper.create = loopback_create;
  loopback_oper.open = loopback_open;
  loopback_oper.read = loopback_read;
  loopback_oper.read_buf = loopback_read_buf;
  loopback_oper.write = loopback_write;
  loopback_oper.write_buf = loopback_write_buf;
  loopback_oper.statfs = loopback_statfs;
  loopback_oper.flush = loopback_flush;
  loopback_oper.release = loopback_release;
  loopback_oper.fsync = loopback_fsync;
  loopback_oper.setxattr = loopback_setxattr;
  loopback_oper.getxattr = loopback_getxattr;
  loopback_oper.listxattr = loopback_listxattr;
  loopback_oper.removexattr = loopback_removexattr;
#ifdef __APPLE__
  loopback_oper.exchange = loopback_exchange;
  loopback_oper.getxtimes = loopback_getxtimes;
  loopback_oper.setattr_x = loopback_setattr_x;
  loopback_oper.fsetattr_x = loopback_fsetattr_x;
  loopback_oper.setvolname = loopback_setvolname;
#endif
#ifndef __APPLE__
  loopback_oper.lock = loopback_lock;
#endif
  loopback_oper.flock = loopback_flock;

  loopback_oper.flag_nullpath_ok = 1;
#ifndef __APPLE__
  loopback_oper.flag_utime_omit_ok = 1;
#endif

  res = fuse_main(argc, argv, &loopback_oper, NULL);
  fuse_opt_free_args(&args);
  return res;
}
