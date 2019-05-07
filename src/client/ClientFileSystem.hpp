#ifndef __CODEFS_CLIENT_FILE_SYSTEM_H__
#define __CODEFS_CLIENT_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ClientFileSystem : public FileSystem {
 public:
  explicit ClientFileSystem(const string& _rootPath) : FileSystem(_rootPath) {}

  virtual ~ClientFileSystem() {}

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

  inline void invalidatePathAndParentAndChildren(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    invalidatePathAndParent(path);
    for (auto& it : allFileData) {
      if (it.first.find(path + string("/")) == 0) {
        LOG(INFO) << "INVALIDATING " << it.first;
        it.second.set_invalid(true);
      }
    }
  }

  inline vector<string> getPathsToDownload(const string& path) {
    auto it = allFileData.find(path);
    if (it != allFileData.end()) {
      // We already have this path, let's make sure we also have all the
      // children
      const auto& fd = it->second;
      if (fd.child_node_size() == 0) {
        return {};
      }
      bool haveAllChildren = true;
      for (const auto& childName : fd.child_node()) {
        auto childPath = (boost::filesystem::path(path) / childName).string();
        if (allFileData.find(childPath) == allFileData.end()) {
          haveAllChildren = false;
          break;
        }
      }
      if (haveAllChildren) {
        return {};
      } else {
        return {path};
      }
    }
    if (path == string("/")) {
      LOGFATAL << "Somehow we don't have the root path???";
    }
    auto parentPath = boost::filesystem::path(path).parent_path().string();
    vector<string> retval = getPathsToDownload(parentPath);
    if (retval.empty()) {
      // If we know the parent directory, then we know all children of the
      // parent directory, so this file doesn't exist and doesn't need to be
      // scanned
    } else {
      retval.push_back(path);
    }
    return retval;
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