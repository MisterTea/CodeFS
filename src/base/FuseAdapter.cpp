#include "Headers.hpp"

#include "FileSystem.hpp"
#include "FuseAdapter.hpp"

namespace codefs {
shared_ptr<FileSystem> fileSystem;

static int codefs_getattr(const char *path, struct stat *stbuf) {
  if (stbuf == NULL) {
    LOG(FATAL) << "Tried to getattr with a NULL stat object";
  }

  const FileData *fileData = fileSystem->getNode(path);
  if (fileData == NULL) {
    LOG(INFO) << "MISSING FILE NODE FOR " << path;
    return -1 * ENOENT;
  }
  FileSystem::protoToStat(fileData->stat_data(), stbuf);
  return 0;
}

static int codefs_fgetattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
  LOG(INFO) << "GETTING ATTR FOR FD " << fi->fh;
  auto it = fileSystem->fdMap.find(fi->fh);
  if (it == fileSystem->fdMap.end()) {
    LOG(INFO) << "MISSING FD";
    errno = EBADF;
    return -errno;
  }
  return codefs_getattr(it->second.path.c_str(), stbuf);
}

static int codefs_access(const char *path, int mask) {
  const FileData *fileData = fileSystem->getNode(path);
  if (fileData == NULL) {
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
  const FileData *fileData = fileSystem->getNode(path);
  if (fileData == NULL) {
    return -ENOENT;
  }

  auto contentsSize = fileData->symlink_contents().length();
  if (contentsSize == 0) {
    return -EINVAL;
  }
  if (contentsSize >= size) {
    memcpy(buf, fileData->symlink_contents().c_str(), size);
    return size;
  } else {
    memcpy(buf, fileData->symlink_contents().c_str(), contentsSize);
    return contentsSize;
  }
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
  while (1) {
    auto node = fileSystem->getNode(d->directory);
    LOG(INFO) << "NUM CHILDREN: " << node->child_node_size();
    if (node->child_node_size() <= d->offset) {
      break;
    }
    string fileName = node->child_node(d->offset);
    string filePath = (boost::filesystem::path(node->path()) /
                       boost::filesystem::path(fileName))
                          .string();

    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    auto childNode = fileSystem->getNode(filePath);
    FileSystem::protoToStat(childNode->stat_data(), &st);
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
  const FileData *fileData = fileSystem->getNode(path);
  if (fileData == NULL) {
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
                           size_t size, uint32_t position) {
  if (position) {
    LOG(FATAL) << "Got a non-zero position: " << position;
  }

  const FileData *fileData = fileSystem->getNode(path);
  if (fileData == NULL) {
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
  return -ENOATTR;
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
  ops->getattr = codefs_getattr;
  ops->fgetattr = codefs_fgetattr;
  ops->access = codefs_access;
  ops->readlink = codefs_readlink;
  ops->opendir = codefs_opendir;
  ops->readdir = codefs_readdir;
  ops->releasedir = codefs_releasedir;
  ops->getxattr = codefs_getxattr;
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
