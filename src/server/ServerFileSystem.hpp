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

  void rescan(const string &absolutePath);
  inline void rescanPathAndParent(const string &absolutePath) {
    rescan(absolutePath);
    rescan(boost::filesystem::path(absolutePath).parent_path().string());
  }

  string readFile(const string &path);
  int writeFile(const string &path, const string &fileContents);

 protected:
  bool initialized;
  Handler *handler;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_FILE_SYSTEM_H__