#ifndef __CODEFS_SERVER_FILE_SYSTEM_H__
#define __CODEFS_SERVER_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ServerFileSystem : public FileSystem {
 public:
  explicit ServerFileSystem(const string &_absoluteFuseRoot);
  virtual ~ServerFileSystem() {}
  void rescan(const string &absolutePath);
  inline void rescanPathAndParent(const string &absolutePath) {
    rescan(absolutePath);
    rescan(boost::filesystem::path(absolutePath).parent_path().string());
  }

 protected:
  unordered_set<string> pathsToRescan;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_FILE_SYSTEM_H__