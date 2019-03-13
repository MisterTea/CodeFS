#include "Headers.hpp"

#include "FileSystem.hpp"
#include "FuseAdapter.hpp"

namespace codefs {
shared_ptr<FileSystem> fileSystem;

static void *codefs_init(struct fuse_conn_info *conn) { return NULL; }

static int codefs_access(const char *path, int mask) {
  optional<FileData> fileData = fileSystem->getNode(path);
  if (!fileData) {
    return -1 * ENOENT;
  }
  LOG(INFO) << "CHECKING ACCESS FOR " << path << " " << mask;
  if (mask == 0) {
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
  optional<FileData> fileData = fileSystem->getNode(path);
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

static int codefs_opendir(const char *path, struct fuse_file_info *fi) {
  LOG(INFO) << "OPENING DIRECTORY AT PATH: " << path;
  FileSystem::DirectoryPointer *d =
      new FileSystem::DirectoryPointer(string(path));
  if (d == NULL) return -ENOMEM;

  if (fileSystem->hasDirectory(path) == false) {
    return -ENOENT;
  }
  fileSystem->dirpMap.insert(make_pair((unsigned long)d, d));
  fi->fh = (unsigned long)d;

  return 0;
}

static inline FileSystem::DirectoryPointer *get_dirp(fuse_file_info *fi) {
  auto it = fileSystem->dirpMap.find((unsigned long)(fi->fh));
  if (it == fileSystem->dirpMap.end()) {
    LOG(FATAL) << "Unable to find directory pointer: " << fi->fh;
  }
  return it->second;
}

static int codefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi) {
  LOG(INFO) << "READING DIRECTORY AT PATH: " << path;
  FileSystem::DirectoryPointer *d = get_dirp(fi);

  if (offset != d->offset) {
    d->offset = offset;
  }
  optional<FileData> node;
  vector<FileData> children;
  node = fileSystem->getNodeAndChildren(d->directory, &children);
  if (!node) {
    // Directory is gone
    return 0;
  }
  while (1) {
    LOG(INFO) << "NUM CHILDREN: " << children.size();
    if (children.size() <= d->offset) {
      break;
    }
    const auto &child = children.at(d->offset);
    string fileName = boost::filesystem::path(child.path()).filename().string();

    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    FileSystem::protoToStat(child.stat_data(), &st);
    if (filler(buf, fileName.c_str(), &st, d->offset + 1)) break;
    (d->offset)++;
  }

  return 0;
}

static int codefs_releasedir(const char *path, struct fuse_file_info *fi) {
  LOG(INFO) << "RELEASING DIRECTORY AT PATH: " << path;
  FileSystem::DirectoryPointer *d = get_dirp(fi);
  fileSystem->dirpMap.erase((unsigned long)d);
  delete d;
  return 0;
}

static int codefs_listxattr(const char *path, char *list, size_t size) {
  optional<FileData> fileData = fileSystem->getNode(path);
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
  optional<FileData> fileData = fileSystem->getNode(path);
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

static int codefs_getxattr_osx(const char *path, const char *name, char *value,
                               size_t size, uint32_t position) {
  if (position) {
    LOG(FATAL) << "Got a non-zero position: " << position;
  }
  return codefs_getxattr(path, name, value, size);
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
  auto owner = fi->lock_owner;
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
      LOG(FATAL) << "This can't happen because FUSE is single threaded.";
      break;
    default:
      LOG(FATAL) << "Invalid lock command";
  }
  return 0;
}

static int codefs_flock(const char *path, struct fuse_file_info *fi, int op) {
  LOG(INFO) << "FLOCK CALLED";
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

void FuseAdapter::assignCallbacks(shared_ptr<FileSystem> _fileSystem,
                                  fuse_operations *ops) {
  if (fileSystem.get()) {
    LOG(FATAL) << "Already initialized FUSE ops!";
  }
  fileSystem = _fileSystem;
  ops->init = codefs_init;
  ops->access = codefs_access;
  ops->readlink = codefs_readlink;
  ops->opendir = codefs_opendir;
  ops->readdir = codefs_readdir;
  ops->releasedir = codefs_releasedir;
  ops->fsync = codefs_fsync;
  ops->fsyncdir = codefs_fsyncdir;
  ops->lock = codefs_lock;
  // ops->flock = codefs_flock;
#if __APPLE__
  ops->getxattr = codefs_getxattr_osx;
#else
  ops->getxattr = codefs_getxattr;
#endif
  ops->listxattr = codefs_listxattr;
}

#if 0
int aaa(int argc, char *argv[]) {
  int res = fuse_main(argc, argv, &codefs_oper, NULL);
  fuse_opt_free_args(&args);
  return res;
}
#endif

}  // namespace codefs
