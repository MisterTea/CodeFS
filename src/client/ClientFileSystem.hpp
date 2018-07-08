#ifndef __CODEFS_CLIENT_FILE_SYSTEM_H__
#define __CODEFS_CLIENT_FILE_SYSTEM_H__

#include "FileSystem.hpp"

namespace codefs {
class ClientFileSystem : public FileSystem {
 public:
  explicit ClientFileSystem(const string &_absoluteFuseRoot)
      : FileSystem(_absoluteFuseRoot) {}
  virtual ~ClientFileSystem() {}
};
}  // namespace codefs

#endif  // __CODEFS_CLIENT_FILE_SYSTEM_H__