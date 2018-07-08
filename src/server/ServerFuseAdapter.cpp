#include "Headers.hpp"

#include "ServerFuseAdapter.hpp"
#include "ServerFileSystem.hpp"

namespace codefs {
shared_ptr<ServerFileSystem> serverFileSystem;

static int codefs_mkdir(const char *path, mode_t mode) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = mkdir(absolutePath.c_str(), mode);
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);

  return 0;
}

static int codefs_unlink(const char *path) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = unlink(absolutePath.c_str());
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_rmdir(const char *path) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = rmdir(absolutePath.c_str());
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_symlink(const char *from, const char *to) {
  int res;

  string absoluteFrom = serverFileSystem->fuseToAbsolute(from);
  string absoluteTo = serverFileSystem->fuseToAbsolute(to);
  res = symlink(absoluteFrom.c_str(), absoluteTo.c_str());
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absoluteFrom);
  serverFileSystem->rescanPathAndParent(absoluteTo);
  return 0;
}

static int codefs_rename(const char *from, const char *to) {
  int res;

  string absoluteFrom = serverFileSystem->fuseToAbsolute(from);
  string absoluteTo = serverFileSystem->fuseToAbsolute(to);
  res = rename(absoluteFrom.c_str(), absoluteTo.c_str());
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absoluteFrom);
  serverFileSystem->rescanPathAndParent(absoluteTo);
  return 0;
}

static int codefs_link(const char *from, const char *to) {
  int res;

  string absoluteFrom = serverFileSystem->fuseToAbsolute(from);
  string absoluteTo = serverFileSystem->fuseToAbsolute(to);
  res = link(absoluteFrom.c_str(), absoluteTo.c_str());
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absoluteFrom);
  serverFileSystem->rescanPathAndParent(absoluteTo);
  return 0;
}

static int codefs_chmod(const char *path, mode_t mode) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = chmod(absolutePath.c_str(), mode);
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_chown(const char *path, uid_t uid, gid_t gid) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = lchown(absolutePath.c_str(), uid, gid);
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_truncate(const char *path, off_t size) {
  int res;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  res = truncate(absolutePath.c_str(), size);
  if (res == -1) return -errno;

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_ftruncate(const char *path, off_t size,
                              struct fuse_file_info *fi) {
  int res;
  LOG(FATAL) << "Not implemented yet";

  (void)path;

  res = ftruncate(fi->fh, size);
  if (res == -1) return -errno;

  return 0;
}

static int codefs_create(const char *path, mode_t mode,
                           struct fuse_file_info *fi) {
  int fd;

  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  fd = open(absolutePath.c_str(), fi->flags, mode);
  if (fd == -1) return -errno;

  fi->fh = fd;
  LOG(INFO) << "CREATING FD " << fi->fh;
  serverFileSystem->fdMap.insert(make_pair((int64_t)fd, FileSystem::FdInfo(string(path))));
  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_open(const char *path, struct fuse_file_info *fi) {
  int fd;

  int modes = 0;
  int readWriteMode = (fi->flags & O_ACCMODE);
  if (readWriteMode == O_RDONLY) {
    // The file is opened in read-only mode
    modes++;
  }
  if (readWriteMode == O_WRONLY) {
    // The file is opened in write-only mode.
    modes++;
    if (fi->flags & O_APPEND) {
      // We need to get the file from the server to append
    }
  }
  if (readWriteMode == O_RDWR) {
    // The file is opened in read-write mode.
    modes++;
  }
  if (modes != 1) {
    LOG(FATAL) << "Invalid open modes: " << fi->flags;
  }
  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  fd = open(absolutePath.c_str(), fi->flags);
  if (fd == -1) return -errno;

  fi->fh = fd;
  LOG(INFO) << "OPENING FD " << fi->fh;
  serverFileSystem->fdMap.insert(make_pair((int64_t)fd, FileSystem::FdInfo(string(path))));
  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
  int res;

  (void)path;
  res = pread(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int codefs_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi) {
  int res;

  (void)path;
  res = pwrite(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int codefs_statfs(const char *path, struct statvfs *stbuf) {
  int res;

  res = statvfs(path, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int codefs_release(const char *path, struct fuse_file_info *fi) {
  LOG(INFO) << "RELEASING FD " << fi->fh;
  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  auto it = serverFileSystem->fdMap.find((int64_t)(fi->fh));
  if (it == serverFileSystem->fdMap.end()) {
    LOG(FATAL) << "Tried to close an fd that doesn't exist";
  }
  serverFileSystem->fdMap.erase(it);
  close(fi->fh);

  serverFileSystem->rescanPathAndParent(absolutePath);
  return 0;
}

static int codefs_fsync(const char *path, int isdatasync,
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

static int codefs_utimens(const char *path, const struct timespec ts[2]) {
  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  int res;

  /* don't use utime/utimes since they follow symlinks */
  res = utimensat(0, absolutePath.c_str(), ts, AT_SYMLINK_NOFOLLOW);
  if (res == -1) return -errno;

  serverFileSystem->rescan(absolutePath);
  return 0;
}

static int codefs_removexattr(const char *path, const char *name) {
  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  int res = lremovexattr(absolutePath.c_str(), name);
  if (res == -1) return -errno;
  serverFileSystem->rescan(absolutePath);
  return 0;
}

static int codefs_setxattr(const char *path, const char *name,
                             const char *value, size_t size, int flags,
                             uint32_t position) {
  if (position) {
    LOG(FATAL) << "Got a non-zero position: " << position;
  }
  string absolutePath = serverFileSystem->fuseToAbsolute(path);
  int res = lsetxattr(absolutePath.c_str(), name, value, size, flags);
  if (res == -1) return -errno;
  serverFileSystem->rescan(absolutePath);
  return 0;
}

#if 0
static const struct fuse_opt codefs_opts[] = {
    // { "case_insensitive", offsetof(struct loopback, case_insensitive), 1 },
    FUSE_OPT_END};

int main_unused(int argc, char *argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  if (fuse_opt_parse(&args, NULL, codefs_opts, NULL) == -1) {
    exit(1);
  }

  umask(0);
  fuse_operations codefs_oper;
  memset(&codefs_oper, 0, sizeof(fuse_operations));
  }
  #endif
  
void ServerFuseAdapter::assignServerCallbacks(shared_ptr<ServerFileSystem> _fileSystem, fuse_operations* ops)  {
  assignCallbacks(_fileSystem, ops);
  if (serverFileSystem.get()) {
    LOG(FATAL) << "Already initialized FUSE ops!";
  }
  serverFileSystem = _fileSystem;
  ops->mkdir = codefs_mkdir;
  ops->symlink = codefs_symlink;
  ops->unlink = codefs_unlink;
  ops->rmdir = codefs_rmdir;
  ops->rename = codefs_rename;
  ops->link = codefs_link;
  ops->chmod = codefs_chmod;
  ops->chown = codefs_chown;
  ops->truncate = codefs_truncate;
  ops->ftruncate = codefs_ftruncate;
  ops->utimens = codefs_utimens;
  ops->create = codefs_create;
  ops->open = codefs_open;
  ops->read = codefs_read;
  ops->write = codefs_write;
  ops->statfs = codefs_statfs;
  ops->release = codefs_release;
  ops->fsync = codefs_fsync;
  ops->setxattr = codefs_setxattr;
  ops->removexattr = codefs_removexattr;
}

#if 0
int aaa(int argc, char *argv[]) {
  int res = fuse_main(argc, argv, &codefs_oper, NULL);
  fuse_opt_free_args(&args);
  return res;
}
#endif

}  // namespace codefs
