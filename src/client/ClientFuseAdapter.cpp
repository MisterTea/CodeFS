#include "Headers.hpp"

#include "Client.hpp"
#include "ClientFileSystem.hpp"
#include "ClientFuseAdapter.hpp"

namespace codefs {
shared_ptr<ClientFileSystem> clientFileSystem;
shared_ptr<Client> client;

static int codefs_getattr(const char *path, struct stat *stbuf) {
  if (stbuf == NULL) {
    LOG(FATAL) << "Tried to getattr with a NULL stat object";
  }

  optional<FileData> fileData = clientFileSystem->getNode(path);
  if (!fileData) {
    LOG(INFO) << "MISSING FILE NODE FOR " << path;
    return -1 * ENOENT;
  }
  optional<int64_t> fileSizeOverride = client->getSizeOverride(path);
  if (fileSizeOverride) {
    fileData->mutable_stat_data()->set_size(*fileSizeOverride);
  }
  FileSystem::protoToStat(fileData->stat_data(), stbuf);
  return 0;
}

static int codefs_fgetattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
  LOG(INFO) << "GETTING ATTR FOR FD " << fi->fh;
  auto it = clientFileSystem->fdMap.find(fi->fh);
  if (it == clientFileSystem->fdMap.end()) {
    LOG(INFO) << "MISSING FD";
    errno = EBADF;
    return -errno;
  }
  return codefs_getattr(it->second.path.c_str(), stbuf);
}

static int codefs_mkdir(const char *path, mode_t mode) {
  int res = client->mkdir(path, mode);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_unlink(const char *path) {
  int res = client->unlink(path);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_rmdir(const char *path) {
  int res = client->rmdir(path);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_symlink(const char *from, const char *to) {
  int res = client->symlink(from, to);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_rename(const char *from, const char *to) {
  int res = client->rename(from, to);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_link(const char *from, const char *to) {
  int res = client->link(from, to);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_chmod(const char *path, mode_t mode) {
  int res = client->chmod(path, mode);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_chown(const char *path, uid_t uid, gid_t gid) {
  int res = client->lchown(path, uid, gid);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_truncate(const char *path, off_t size) {
  int res = client->truncate(path, size);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_ftruncate(const char *path, off_t size,
                            struct fuse_file_info *fi) {
  return codefs_truncate(path, size);
}

static int codefs_create(const char *path, mode_t mode,
                         struct fuse_file_info *fi) {
  int fd = client->open(path, fi->flags, mode);
  if (fd == -1) return -errno;
  fi->fh = fd;
  LOG(INFO) << "CREATING FD " << fi->fh << " FOR PATH " << path;
  clientFileSystem->fdMap.insert(
      make_pair((int64_t)fd, FileSystem::FdInfo(string(path))));
  return 0;
}

static int codefs_open(const char *path, struct fuse_file_info *fi) {
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
      LOG(FATAL) << "APPEND NOT SUPPORTED YET";
    }
  }
  if (readWriteMode == O_RDWR) {
    // The file is opened in read-write mode.
    modes++;
  }
  if (modes != 1) {
    LOG(FATAL) << "Invalid open modes: " << fi->flags;
  }
  int fd = client->open(path, fi->flags, readWriteMode);
  if (fd == -1) return -errno;

  fi->fh = fd;
  LOG(INFO) << "OPENING FD " << fi->fh << " WITH MODE " << readWriteMode;
  clientFileSystem->fdMap.insert(
      make_pair((int64_t)fd, FileSystem::FdInfo(string(path))));
  return 0;
}

static int codefs_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
  int res;

  res = client->pread(path, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int codefs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
  int res;

  LOG(INFO) << "IN WRITE: " << path << ":" << offset;
  res = client->pwrite(path, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int codefs_statfs(const char *path, struct statvfs *stbuf) {
  int res;

  LOG(INFO) << "BEGIN CLIENT STATFS";
  res = client->statvfs(stbuf);
  LOG(INFO) << "END CLIENT STATFS";
  if (res == -1) return -errno;

  return 0;
}

static int codefs_release(const char *path, struct fuse_file_info *fi) {
  int fd = fi->fh;
  LOG(INFO) << "RELEASING " << path << " FD " << fd;
  auto it = clientFileSystem->fdMap.find((int64_t)(fd));
  if (it == clientFileSystem->fdMap.end()) {
    LOG(FATAL) << "Tried to close an fd that doesn't exist";
  }
  string pathFromFd = it->second.path;
  if (pathFromFd != string(path)) {
    LOG(ERROR) << "PATHS DO NOT MATCH! " << pathFromFd << " " << path;
  }
  clientFileSystem->fdMap.erase(it);
  client->close(path, fd);
  return 0;
}

static int codefs_utimens(const char *path, const struct timespec ts[2]) {
  int res;

  /* don't use utime/utimes since they follow symlinks */
  res = client->utimensat(path, ts);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_removexattr(const char *path, const char *name) {
  int res = client->lremovexattr(path, name);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_setxattr(const char *path, const char *name,
                           const char *value, size_t size, int flags) {
  int res = client->lsetxattr(path, name, value, size, flags);
  if (res == -1) return -errno;
  return 0;
}

static int codefs_setxattr_osx(const char *path, const char *name,
                               const char *value, size_t size, int flags,
                               uint32_t position) {
  if (position) {
    LOG(FATAL) << "Got a non-zero position: " << position;
  }
  return codefs_setxattr(path, name, value, size, flags);
}

void ClientFuseAdapter::assignClientCallbacks(
    shared_ptr<ClientFileSystem> _fileSystem, shared_ptr<Client> _client,
    fuse_operations *ops) {
  assignCallbacks(_fileSystem, ops);
  if (clientFileSystem.get()) {
    LOG(FATAL) << "Already initialized FUSE ops!";
  }
  clientFileSystem = _fileSystem;
  client = _client;
  ops->mkdir = codefs_mkdir;
  ops->symlink = codefs_symlink;
  ops->getattr = codefs_getattr;
  ops->fgetattr = codefs_fgetattr;
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
#if __APPLE__
  ops->setxattr = codefs_setxattr_osx;
#else
  ops->setxattr = codefs_setxattr;
#endif
  ops->removexattr = codefs_removexattr;
}

}  // namespace codefs
