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
    rescanPath(absolutePath);
    if (absoluteToRelative(absolutePath) != string("/")) {
      rescanPath(boost::filesystem::path(absolutePath).parent_path().string());
    }
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
    return ::symlink(from.c_str(), relativeToAbsolute(to).c_str());
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
    return ::chmod(relativeToAbsolute(path).c_str(), mode);
  }

  int lchown(const string &path, int64_t uid, int64_t gid) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::lchown(relativeToAbsolute(path).c_str(), uid, gid);
  }

  int truncate(const string &path, int64_t size) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::truncate(relativeToAbsolute(path).c_str(), size);
  }

  int utimensat(const string &path, struct timespec ts[2]) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::utimensat(0, relativeToAbsolute(path).c_str(), ts,
                       AT_SYMLINK_NOFOLLOW);
  }

  int lremovexattr(const string &path, const string &name) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::lremovexattr(relativeToAbsolute(path).c_str(), name.c_str());
  }

  int lsetxattr(const string &path, const string &name, const string &value,
                int64_t size, int flags) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    return ::lsetxattr(relativeToAbsolute(path).c_str(), name.c_str(),
                       value.c_str(), size, flags);
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