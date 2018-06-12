#ifndef __CODEFS_SERVER_FILE_SYSTEM_H__
#define __CODEFS_SERVER_FILE_SYSTEM_H__

#include "FileSystem.hpp"

#include "Server.hpp"

namespace codefs {
class ServerFileSystem : public FileSystem {
 public:
  explicit ServerFileSystem(const string &_absoluteFuseRoot);
  virtual ~ServerFileSystem() {}
  virtual void write(const string &path, const string &data) {}
  virtual string read(const string &path) {}
  virtual void startFuse() {}
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_FILE_SYSTEM_H__