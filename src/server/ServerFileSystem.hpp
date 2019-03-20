#ifndef __CODEFS_SERVER_FILE_SYSTEM_H__
#define __CODEFS_SERVER_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ServerFileSystem : public FileSystem {
 public:
  class Handler {
   public:
    virtual void metadataUpdated(const string &path,
                                 const FileData &fileData) = 0;
  };

  explicit ServerFileSystem(const string &_rootPath);
  virtual ~ServerFileSystem() {}

  void init();
  inline bool isInitialized() { return initialized; }
  void setHandler(Handler *_handler) { handler = _handler; }

  void rescanPath(const string &absolutePath);

  inline void rescanPathAndParent(const string &absolutePath) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    rescanPath(absolutePath);
    if (absoluteToRelative(absolutePath) != string("/")) {
      LOG(INFO) << "RESCANNING PARENT";
      rescanPath(boost::filesystem::path(absolutePath).parent_path().string());
    }
  }

  inline void rescanPathAndParentAndChildren(const string &absolutePath) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    if (absoluteToRelative(absolutePath) != string("/")) {
      rescanPath(boost::filesystem::path(absolutePath).parent_path().string());
    }
    rescanPathAndChildren(absolutePath);
  }

  inline void rescanPathAndChildren(const string &absolutePath) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    auto node = getNode(absolutePath);
    if (node) {
      // scan node and known children envelope for deletion/update
      string relativePath = absoluteToRelative(absolutePath);
      vector<string> subPaths;
      for (auto &it : allFileData) {
        if (it.first.find(relativePath) == 0) {
          // This is the node or a child
          subPaths.push_back(it.first);
        }
      }
      for (auto &it : subPaths) {
        rescanPath(relativeToAbsolute(it));
      }
    }

    // Begin recursive scan to pick up new children
    scanRecursively(absolutePath, &allFileData);
  }

  string readFile(const string &path);
  int writeFile(const string &path, const string &fileContents);

  int mkdir(const string &path, mode_t mode) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::mkdir(relativeToAbsolute(path).c_str(), mode);
  }

  int unlink(const string &path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::unlink(relativeToAbsolute(path).c_str());
  }

  int rmdir(const string &path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::rmdir(relativeToAbsolute(path).c_str());
  }

  int symlink(const string &from, const string &to) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::symlink(relativeToAbsolute(from).c_str(),
                     relativeToAbsolute(to).c_str());
  }

  int link(const string &from, const string &to) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::link(relativeToAbsolute(from).c_str(),
                  relativeToAbsolute(to).c_str());
  }

  int rename(const string &from, const string &to) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::rename(relativeToAbsolute(from).c_str(),
                    relativeToAbsolute(to).c_str());
  }

  int chmod(const string &path, mode_t mode) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::chmod(relativeToAbsolute(path).c_str(), mode);
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  int lchown(const string &path, int64_t uid, int64_t gid) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::lchown(relativeToAbsolute(path).c_str(), uid, gid);
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  int truncate(const string &path, int64_t size) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::truncate(relativeToAbsolute(path).c_str(), size);
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  int utimensat(const string &path, struct timespec ts[2]) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::utimensat(0, relativeToAbsolute(path).c_str(), ts,
                          AT_SYMLINK_NOFOLLOW);
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  int lremovexattr(const string &path, const string &name) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::lremovexattr(relativeToAbsolute(path).c_str(), name.c_str());
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  int lsetxattr(const string &path, const string &name, const string &value,
                int64_t size, int flags) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    int res = ::lsetxattr(relativeToAbsolute(path).c_str(), name.c_str(),
                          value.c_str(), size, flags);
    rescanPath(relativeToAbsolute(path));
    return res;
  }

  void scanRecursively(const string &path,
                       unordered_map<string, FileData> *result);
  FileData scanNode(const string &path,
                    unordered_map<string, FileData> *result);

 protected:
  bool initialized;
  Handler *handler;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_FILE_SYSTEM_H__