#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include "Headers.hpp"

namespace codefs {
class FileSystem {
 public:
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

  explicit FileSystem(const string &_rootPath) : rootPath(_rootPath) {
    boost::trim_right_if(rootPath, boost::is_any_of("/"));
  }

  virtual optional<FileData> getNode(const string &path) {
    while (true) {
      {
        std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
        auto it = allFileData.find(path);
        if (it == allFileData.end()) {
          return nullopt;
        }
        bool invalid = it->second.invalid();
        if (!invalid) {
          return it->second;
        } else {
          LOG(INFO) << path << " is invalid, waiting for new version";
        }
      }
      usleep(100 * 1000);
    }
  }

  virtual optional<FileData> getNodeAndChildren(const string &path,
                                                vector<FileData> *children) {
    vector<FileData> tmpChildren;
    while (true) {
      {
        std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
        auto it = allFileData.find(path);
        if (it == allFileData.end()) {
          return nullopt;
        }
        bool invalid = it->second.invalid();
        if (!invalid) {
          FileData parentNode = it->second;

          // Check the children
          LOG(INFO) << "NUM CHILDREN: " << parentNode.child_node_size();
          bool failed = false;
          tmpChildren.clear();
          for (int a = 0; a < parentNode.child_node_size(); a++) {
            string fileName = parentNode.child_node(a);
            string childPath = (boost::filesystem::path(parentNode.path()) /
                                boost::filesystem::path(fileName))
                                   .string();

            auto childIt = allFileData.find(childPath);
            if (childIt == allFileData.end() || childIt->second.invalid()) {
              if (childIt == allFileData.end())
                LOG(INFO) << "MISSING CHILD: " << childPath;
              else
                LOG(INFO) << "INVALID CHILD: " << childPath;
              failed = true;
              break;
            }
            tmpChildren.push_back(childIt->second);
          }

          if (!failed) {
            *children = tmpChildren;
            return parentNode;
          }
        } else {
          LOG(INFO) << path << " is invalid, waiting for new version";
        }
      }
      usleep(100 * 1000);
    }
  }

  void setNode(const FileData &fileData) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    allFileData.erase(fileData.path());
    if (fileData.deleted()) {
      // The node is deleted, Don't add
      LOG(INFO) << fileData.path() << " was deleted!";
    } else {
      if (fileData.invalid()) {
        LOG(INFO) << "INVALIDAING " << fileData.path();
      }
      allFileData.insert(make_pair(fileData.path(), fileData));
    }
  }

  void createStub(const string &path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    FileData stub;
    stub.set_path(path);
    stub.set_invalid(true);
    setNode(stub);
  }

  void deleteNode(const string &path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    auto it = allFileData.find(path);
    if (it != allFileData.end()) {
      allFileData.erase(it);
    }
  }

  virtual string absoluteToRelative(const string &absolutePath) {
    if (absolutePath.find(rootPath) != 0) {
      LOG(FATAL) << "Tried to convert absolute path to fuse that wasn't inside "
                    "the absolute FS: "
                 << absolutePath << " " << rootPath;
    }
    string relative = absolutePath.substr(rootPath.size());
    if (relative.length() == 0) {
      return "/";
    } else {
      return relative;
    }
  }
  virtual string relativeToAbsolute(const string &relativePath) {
    return (boost::filesystem::path(rootPath) / relativePath).string();
  }

  inline bool hasDirectory(const string &path) {
    auto fileData = getNode(path);
    return fileData && S_ISDIR(fileData->stat_data().mode());
  }

  static inline void statToProto(const struct stat &fileStat, StatData *fStat) {
    fStat->set_dev(fileStat.st_dev);
    fStat->set_ino(fileStat.st_ino);
    fStat->set_mode(fileStat.st_mode);
    fStat->set_nlink(fileStat.st_nlink);
    fStat->set_uid(fileStat.st_uid);
    fStat->set_gid(fileStat.st_gid);
    fStat->set_rdev(fileStat.st_rdev);
    fStat->set_size(fileStat.st_size);
    fStat->set_blksize(fileStat.st_blksize);
    fStat->set_blocks(fileStat.st_blocks);
    fStat->set_atime(fileStat.st_atime);
    fStat->set_mtime(fileStat.st_mtime);
    fStat->set_ctime(fileStat.st_ctime);
  }

  static inline void protoToStat(const StatData &fStat, struct stat *fileStat) {
    fileStat->st_dev = fStat.dev();
    fileStat->st_ino = fStat.ino();
    fileStat->st_mode = fStat.mode();
    fileStat->st_nlink = fStat.nlink();
    fileStat->st_uid = fStat.uid();
    fileStat->st_gid = fStat.gid();
    fileStat->st_rdev = fStat.rdev();
    fileStat->st_size = fStat.size();
    fileStat->st_blksize = fStat.blksize();
    fileStat->st_blocks = fStat.blocks();
    fileStat->st_atime = fStat.atime();
    fileStat->st_mtime = fStat.mtime();
    fileStat->st_ctime = fStat.ctime();
  }

  string serializeAllFileDataCompressed();
  void deserializeAllFileDataCompressed(const string &s);

  unordered_map<int64_t, FdInfo> fdMap;
  unordered_map<int64_t, DirectoryPointer *> dirpMap;
  unordered_map<string, FileData> allFileData;

 protected:
  string rootPath;
  shared_ptr<thread> fuseThread;
  std::recursive_mutex fileDataMutex;
};
}  // namespace codefs

#ifdef __APPLE__
// Add missing xattr functions for OS/X

#define G_PREFIX "org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX ".apple.system.Security"
inline ssize_t llistxattr(const char *path, char *list, size_t size) {
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
    }
  }

  return res;
}

#define A_PREFIX "com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX ".apple.system.Security"
#define XATTR_APPLE_PREFIX "com.apple."
inline ssize_t lgetxattr(const char *path, const char *name, void *value,
                         size_t size) {
  int res;

  if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = getxattr(path, new_name, value, size, 0, XATTR_NOFOLLOW);

  } else {
    res = getxattr(path, name, value, size, 0, XATTR_NOFOLLOW);
  }

  return res;
}

inline int lsetxattr(const char *path, const char *name, const void *value,
                     size_t size, int flags) {
  int res;

  if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
    flags &= ~(XATTR_NOSECURITY);
  }

  if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = setxattr(path, new_name, value, size, 0, XATTR_NOFOLLOW);

  } else {
    res = setxattr(path, name, value, size, 0, XATTR_NOFOLLOW);
  }

  if (res == -1) {
    return -errno;
  }

  return 0;
}

inline int lremovexattr(const char *path, const char *name) {
  int res;

  if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = removexattr(path, new_name, XATTR_NOFOLLOW);

  } else {
    res = removexattr(path, name, XATTR_NOFOLLOW);
  }

  return res;
}

#endif

#endif  // __FILESYSTEM_H__
