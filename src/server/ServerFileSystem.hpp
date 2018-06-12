#ifndef __CODEFS_SERVER_FILE_SYSTEM_H__
#define __CODEFS_SERVER_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ServerFileSystem : public FileSystem {
 public:
  virtual ~ServerFileSystem() {}
  virtual void write(const string &path, const string &data) {}
  virtual string read(const string &path) {}
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_FILE_SYSTEM_H__