#include "Headers.hpp"

#include "Client.hpp"
#include "ClientFuseAdapter.hpp"

namespace codefs {
shared_ptr<Client> client;
shared_ptr<ClientFileSystem> fileSystem;

class DirectoryPointer {
 public:
  string directory;
  int offset;

  DirectoryPointer(const string &_directory)
      : directory(_directory), offset(0) {}
};

class FdInfo {
 public:
  string path;

  FdInfo(const string &_path) : path(_path) {}
};

unordered_map<int64_t, FdInfo> fdMap;
unordered_map<int64_t, DirectoryPointer *> dirpMap;

static void *codefs_init(struct fuse_conn_info *conn) { return NULL; }

static int codefs_access(const char *path, int mask) {
  VLOG(1) << "CHECKING ACCESS FOR " << path << " " << mask;
  optional<FileData> fileData = client->getNode(path);
  if (!fileData) {
    LOG(INFO) << "FILE DOESN'T EXIST";
    return -1 * ENOENT;
  }
  if (mask == 0) {
    LOG(INFO) << "MASK IS 0";
    return 0;
  }

  if (mask & R_OK) {
    if (fileData->can_read()) {
    } else {
      return -1 * EACCES;
    }
  }
  if (mask & W_OK) {
    if (fileData->can_write()) {
    } else {
      return -1 * EACCES;
    }
  }
  if (mask & X_OK) {
    if (fileData->can_execute()) {
    } else {
      return -1 * EACCES;
    }
  }

  return 0;
}

static int codefs_readlink(const char *path, char *buf, size_t size) {
  optional<FileData> fileData = client->getNode(path);
  if (!fileData) {
    return -ENOENT;
  }

  string absoluteTo = fileData->symlink_contents();
  if (absoluteTo[0] == '/') {
    absoluteTo = fileSystem->relativeToAbsolute(absoluteTo);
  }
  auto contentsSize = absoluteTo.length();
  if (contentsSize == 0) {
    return -EINVAL;
  }
  if (contentsSize >= size) {
    memcpy(buf, absoluteTo.c_str(), size);
  } else {
    memcpy(buf, absoluteTo.c_str(), contentsSize);
  }
  // NOTE: This is different than POSIX readlink which returns the size
  return 0;
}

static int codefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi) {
  VLOG(1) << "READING DIRECTORY AT PATH: " << path;
  if (client->hasDirectory(path) == false) {
    return -ENOENT;
  }
  optional<FileData> node;
  vector<FileData> children;
  node = client->getNodeAndChildren(path, &children);
  for (const auto &child : children) {
    string fileName = boost::filesystem::path(child.path()).filename().string();
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    FileSystem::protoToStat(child.stat_data(), &st);
    if (filler(buf, fileName.c_str(), &st, 0)) {
      LOGFATAL << "Filler returned non-zero value";
      break;
    }
  }
  return 0;
}

static int codefs_listxattr(const char *path, char *list, size_t size) {
  optional<FileData> fileData = client->getNode(path);
  if (!fileData) {
    return -1 * ENOENT;
  }
  string s;
  for (int a = 0; a < fileData->xattr_key().size(); a++) {
    s.append(fileData->xattr_key(a));
    s.append("\0");
  }
  if (s.length() > size) {
    return -ERANGE;
  }
  memcpy(list, s.c_str(), s.length());
  return s.length();
}

static int codefs_getxattr(const char *path, const char *name, char *value,
                           size_t size) {
  optional<FileData> fileData = client->getNode(path);
  if (!fileData) {
    return -1 * ENOENT;
  }
  for (int a = 0; a < fileData->xattr_key().size(); a++) {
    if (fileData->xattr_key(a) == name) {
      if (fileData->xattr_value(a).length() < size) {
        return -ERANGE;
      }
      memcpy(value, &(fileData->xattr_value(a)[0]),
             fileData->xattr_value(a).length());
      return fileData->xattr_value(a).length();
    }
  }
#ifndef ENOATTR
#define ENOATTR (0)
#endif
  return -ENOATTR;
}

static int codefs_fsync(const char *, int, struct fuse_file_info *) {
  return 0;
}

static int codefs_fsyncdir(const char *, int, struct fuse_file_info *) {
  return 0;
}

map<string, struct flock> readLocks;
map<string, struct flock> writeLocks;
static int codefs_lock(const char *path, struct fuse_file_info *fi, int cmd,
                       struct flock *lockp) {
  LOG(INFO) << "LOCK CALLED FOR PATH " << string(path) << " and fh " << fi->fh;
  struct flock lockCopy = *lockp;
  string pathString = string(path);
  switch (cmd) {
    case F_GETLK:
      switch (lockCopy.l_type) {
        case F_WRLCK:
          // write-locks also check for read locks

          // TODO: Check offset and length instead of just file
          if (writeLocks.find(pathString) != writeLocks.end()) {
            *lockp = writeLocks.at(pathString);
          } else if (readLocks.find(pathString) != readLocks.end()) {
            *lockp = readLocks.at(pathString);
          } else {
            lockp->l_type = F_UNLCK;
          }
          break;
        case F_RDLCK:
          // TODO: Check offset and length instead of just file
          if (readLocks.find(pathString) != readLocks.end()) {
            *lockp = readLocks.at(pathString);
          } else {
            lockp->l_type = F_UNLCK;
          }
          break;
      }
      break;
    case F_SETLK:
      switch (lockCopy.l_type) {
        case F_WRLCK:
          if (writeLocks.find(pathString) != writeLocks.end()) {
            auto &lock = writeLocks.at(pathString);
            if (lock.l_pid == lockCopy.l_pid) {
              lock = lockCopy;
            } else {
              return -1 * EAGAIN;
            }
          }
          if (readLocks.find(pathString) != readLocks.end()) {
            auto &lock = readLocks.at(pathString);
            if (lock.l_pid == lockCopy.l_pid) {
              lock = lockCopy;
            } else {
              return -1 * EAGAIN;
            }
          }
          break;
        case F_RDLCK:
          if (readLocks.find(pathString) != readLocks.end()) {
            auto &lock = readLocks.at(pathString);
            if (lock.l_pid == lockCopy.l_pid) {
              lock = lockCopy;
            } else {
              return -1 * EAGAIN;
            }
          }
          break;
        case F_UNLCK:
          if (writeLocks.find(pathString) != writeLocks.end()) {
            auto &lock = writeLocks.at(pathString);
            if (lock.l_pid == lockCopy.l_pid) {
              writeLocks.erase(writeLocks.find(pathString));
            } else {
              return -1 * EAGAIN;
            }
          }
          if (readLocks.find(pathString) != readLocks.end()) {
            auto &lock = readLocks.at(pathString);
            if (lock.l_pid == lockCopy.l_pid) {
              readLocks.erase(readLocks.find(pathString));
            } else {
              return -1 * EAGAIN;
            }
          }
          break;
      }
      break;
    case F_SETLKW:
      LOGFATAL << "This can't happen because FUSE is single threaded.";
      break;
    default:
      LOGFATAL << "Invalid lock command";
  }
  return 0;
}

static int codefs_getattr(const char *path, struct stat *stbuf) {
  if (stbuf == NULL) {
    LOGFATAL << "Tried to getattr with a NULL stat object";
  }

  LOG(INFO) << "GETTING ATTR FOR PATH: " << path;
  optional<FileData> fileDataPtr = client->getNode(path);
  if (!fileDataPtr) {
    LOG(INFO) << "File doesn't exist";
    return -1 * ENOENT;
  }
  FileData fileData = *fileDataPtr;
  optional<int64_t> fileSizeOverride = client->getSizeOverride(path);
  if (fileSizeOverride) {
    fileData.mutable_stat_data()->set_size(*fileSizeOverride);
  }
  FileSystem::protoToStat(fileData.stat_data(), stbuf);
  return 0;
}

static int codefs_fgetattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
  LOG(INFO) << "GETTING ATTR FOR FD " << fi->fh;
  auto it = fdMap.find(fi->fh);
  if (it == fdMap.end()) {
    LOG(INFO) << "MISSING FD";
    errno = EBADF;
    return -errno;
  }
  LOG(INFO) << "PATHS: " << string(path) << " " << it->second.path;
  return codefs_getattr(path, stbuf);
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
  int fd = client->create(path, fi->flags, mode);
  if (fd == -1) return -errno;
  fi->fh = fd;
  LOG(INFO) << "CREATING FD " << fi->fh << " FOR PATH " << path;
  fdMap.insert(make_pair((int64_t)fd, FdInfo(string(path))));
  return 0;
}

static int codefs_open(const char *path, struct fuse_file_info *fi) {
  if (fi->flags & O_CREAT) {
    LOGFATAL << "GOT O_CREAT BUT NO MODE";
  }
  int openModes = 0;
  int readWriteMode = (fi->flags & O_ACCMODE);
  if (readWriteMode == O_RDONLY) {
    // The file is opened in read-only mode
    openModes++;
  }
  if (readWriteMode == O_WRONLY) {
    // The file is opened in write-only mode.
    openModes++;
    if (fi->flags & O_APPEND) {
      // We need to get the file from the server to append
      LOGFATAL << "APPEND NOT SUPPORTED YET";
    }
  }
  if (readWriteMode == O_RDWR) {
    // The file is opened in read-write mode.
    openModes++;
  }
  if (openModes != 1) {
    LOGFATAL << "Invalid open openModes: " << fi->flags;
  }
  int fd = client->open(path, fi->flags);
  if (fd == -1) return -errno;

  fi->fh = fd;
  LOG(INFO) << "OPENING FD " << fi->fh << " WITH MODE " << readWriteMode;
  fdMap.insert(make_pair((int64_t)fd, FdInfo(string(path))));
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

  res = client->statvfs(stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int codefs_release(const char *path, struct fuse_file_info *fi) {
  int fd = fi->fh;
  LOG(INFO) << "RELEASING " << path << " FD " << fd;
  auto it = fdMap.find((int64_t)(fd));
  if (it == fdMap.end()) {
    LOGFATAL << "Tried to close an fd that doesn't exist";
  }
  string pathFromFd = it->second.path;
  if (pathFromFd != string(path)) {
    LOG(ERROR) << "PATHS DO NOT MATCH! " << pathFromFd << " " << path;
  }
  fdMap.erase(it);
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

#if __APPLE__
static int codefs_getxattr_osx(const char *path, const char *name, char *value,
                               size_t size, uint32_t position) {
  if (position) {
    LOGFATAL << "Got a non-zero position: " << position;
  }
  return codefs_getxattr(path, name, value, size);
}

static int codefs_setxattr_osx(const char *path, const char *name,
                               const char *value, size_t size, int flags,
                               uint32_t position) {
  if (position) {
    LOGFATAL << "Got a non-zero position: " << position;
  }
  return codefs_setxattr(path, name, value, size, flags);
}
#endif

void ClientFuseAdapter::assignCallbacks(
    shared_ptr<ClientFileSystem> _fileSystem, shared_ptr<Client> _client,
    fuse_operations *ops) {
  if (fileSystem.get()) {
    LOGFATAL << "Already initialized FUSE ops!";
  }
  fileSystem = _fileSystem;
  client = _client;
  ops->init = codefs_init;
  ops->access = codefs_access;
  ops->readlink = codefs_readlink;
  // ops->opendir = codefs_opendir;
  ops->readdir = codefs_readdir;
  // ops->releasedir = codefs_releasedir;
  ops->fsync = codefs_fsync;
  ops->fsyncdir = codefs_fsyncdir;
  ops->lock = codefs_lock;
  // ops->flock = codefs_flock;

  ops->mkdir = codefs_mkdir;
  ops->symlink = codefs_symlink;
  ops->getattr = codefs_getattr;
  ops->fgetattr = codefs_fgetattr;
  ops->unlink = codefs_unlink;
  ops->rmdir = codefs_rmdir;
  ops->rename = codefs_rename;
  // ops->link = codefs_link;
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
  ops->listxattr = codefs_listxattr;
#if __APPLE__
  ops->setxattr = codefs_setxattr_osx;
  ops->getxattr = codefs_getxattr_osx;
#else
  ops->setxattr = codefs_setxattr;
  ops->getxattr = codefs_getxattr;
#endif
  ops->removexattr = codefs_removexattr;
}

}  // namespace codefs
