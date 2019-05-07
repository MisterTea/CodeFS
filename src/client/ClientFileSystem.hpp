#ifndef __CODEFS_CLIENT_FILE_SYSTEM_H__
#define __CODEFS_CLIENT_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class OwnedFileInfo {
 public:
  unordered_set<int> fds;
  string content;
  bool readOnly;

  OwnedFileInfo() : readOnly(false) {}

  OwnedFileInfo(int fd, string _content, bool _readOnly)
      : content(_content), readOnly(_readOnly) {
    fds.insert(fd);
  }

  explicit OwnedFileInfo(const OwnedFileInfo& other) {
    fds = other.fds;
    content = other.content;
    readOnly = other.readOnly;
  }
};

class ClientFileSystem : public FileSystem {
 public:
  explicit ClientFileSystem(const string& _rootPath)
      : FileSystem(_rootPath), fdCounter(1) {}

  virtual ~ClientFileSystem() {}

  inline void invalidatePath(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    invalidateVfsCache();
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
    std::lock_guard<std::recursive_mutex> lock(mutex);
    invalidateVfsCache();
    invalidatePath(boost::filesystem::path(path).parent_path().string());
    invalidatePath(path);
  }

  inline void invalidatePathAndParentAndChildren(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    invalidateVfsCache();
    invalidatePathAndParent(path);
    for (auto& it : allFileData) {
      if (it.first.find(path + string("/")) == 0) {
        LOG(INFO) << "INVALIDATING " << it.first;
        it.second.set_invalid(true);
      }
    }
  }

  inline vector<string> getPathsToDownload(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
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
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = fileCache.find(path);
    if (it == fileCache.end()) {
      return optional<string>();
    } else {
      return it->second;
    }
  }

  inline void setCachedFile(const string& path, const string& data) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    fileCache[path] = data;
  }

  inline void invalidateVfsCache() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    cachedStatVfsProto.reset();
  }

  inline int getNewFd() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    fdCounter++;
    return fdCounter;
  }

  inline bool ownsPathContents(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return ownedFileContents.find(path) != ownedFileContents.end();
  }

  inline void addOwnedFileContents(const string& path, int fd,
                                   const string& cachedData, bool readOnly) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ownedFileContents[path] = OwnedFileInfo(fd, cachedData, readOnly);
  }

  inline void addHandleToOwnedFile(const string& path, int fd, bool readOnly) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ownedFileContents.at(path).fds.insert(fd);
    if (!readOnly) {
      ownedFileContents.at(path).readOnly = false;
    }
  }

  inline int readOwnedFile(const string& path, char* buf, int64_t size,
                           int64_t offset) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = ownedFileContents.find(path);
    if (it == ownedFileContents.end()) {
      LOGFATAL << "TRIED TO READ AN INVALID PATH";
    }
    const auto& content = it->second.content;
    if (offset >= int64_t(content.size())) {
      return 0;
    }
    auto start = content.c_str() + offset;
    int64_t actualSize = min(int64_t(content.size()), offset + size) - offset;
    LOG(INFO) << content.size() << " " << size << " " << offset << " "
              << actualSize << endl;
    memcpy(buf, start, actualSize);
    return actualSize;
  }

  inline int writeOwnedFile(const string& path, const char* buf, int64_t size,
                            int64_t offset) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = ownedFileContents.find(path);
    if (it == ownedFileContents.end()) {
      LOGFATAL << "TRIED TO READ AN INVALID PATH: " << path;
    }
    if (it->second.readOnly) {
      LOGFATAL << "Tried to write to a read-only file: " << path;
    }
    auto& content = it->second.content;
    LOG(INFO) << "WRITING " << size << " TO " << path << " AT " << offset;
    if (int64_t(content.size()) < offset + size) {
      content.resize(offset + size, '\0');
    }
    memcpy(&(content[offset]), buf, size);
    return size;
  }

  inline void closeOwnedFile(const string& path, int fd, bool* readOnly,
                             string* content) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto& ownedFile = ownedFileContents.at(path);
    if (ownedFile.fds.find(fd) == ownedFile.fds.end()) {
      LOGFATAL << "Tried to close a file handle that is not owned";
    }
    if (!ownedFile.readOnly) {
      LOG(INFO) << "Invalidating path";
      invalidatePathAndParent(path);
    }
    *readOnly = ownedFile.readOnly;
    *content = ownedFile.content;
    ownedFile.fds.erase(ownedFile.fds.find(fd));
    if (ownedFile.fds.empty()) {
      ownedFileContents.erase(path);
    }
  }

  optional<int64_t> getSizeOverride(const string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = ownedFileContents.find(path);
    if (it == ownedFileContents.end()) {
      return optional<int64_t>();
    }
    return int64_t(it->second.content.size());
  }

  bool truncateOwnedFileIfExists(const string& path, int64_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (ownedFileContents.find(path) != ownedFileContents.end()) {
      ownedFileContents.at(path).content.resize(size, '\0');
      return true;
    }
    return false;
  }

  optional<StatVfsData> getVfsCache() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return cachedStatVfsProto;
  }

  void setVfsCache(const StatVfsData& vfs) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    cachedStatVfsProto = vfs;
  }

  void renameOwnedFileIfItExists(const string& from, const string& to) {
    if (ownedFileContents.find(to) != ownedFileContents.end()) {
      LOGFATAL << "I don't handle renaming from one open file to another yet";
    }
    if (ownedFileContents.find(from) != ownedFileContents.end()) {
      ownedFileContents.insert(make_pair(to, ownedFileContents.at(from)));
      ownedFileContents.erase(ownedFileContents.find(from));
    }
  }

 protected:
  unordered_map<string, string> fileCache;
  unordered_map<string, OwnedFileInfo> ownedFileContents;
  optional<StatVfsData> cachedStatVfsProto;
  int fdCounter;
};
}  // namespace codefs

#endif  // __CODEFS_CLIENT_FILE_SYSTEM_H__