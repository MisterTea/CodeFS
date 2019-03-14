#ifndef __CODEFS_CLIENT_FILE_SYSTEM_H__
#define __CODEFS_CLIENT_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ClientFileSystem : public FileSystem {
 public:
  explicit ClientFileSystem(const string& _rootPath) : FileSystem(_rootPath) {}

  virtual ~ClientFileSystem() {}

  void init(const vector<FileData>& initialData) {
    for (const FileData& fd : initialData) {
      allFileData.emplace(fd.path(), fd);
    }
  }

  inline void invalidatePath(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    LOG(INFO) << "INVALIDATING PATH: " << path;
    {
      auto it = fileCache.find(path);
      if (it != fileCache.end()) {
        fileCache.erase(it);
      }
    }
    auto it = allFileData.find(path);
    if (it == allFileData.end()) {
      // Create empty invalid node
      FileData fd;
      fd.set_invalid(true);
      allFileData[path] = fd;
      return;
    }
    it->second.set_invalid(true);
  }

  inline void invalidatePathAndParent(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    invalidatePath(boost::filesystem::path(path).parent_path().string());
    invalidatePath(path);
  }

  inline optional<string> getCachedFile(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    auto it = fileCache.find(path);
    if (it == fileCache.end()) {
      return optional<string>();
    } else {
      return it->second;
    }
  }

  inline void setCachedFile(const string& path, const string& data) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    fileCache[path] = data;
  }

 protected:
  unordered_map<string, string> fileCache;
};
}  // namespace codefs

#endif  // __CODEFS_CLIENT_FILE_SYSTEM_H__