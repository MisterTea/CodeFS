#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include "Headers.hpp"

namespace codefs {
class FileSystem {
 public:
  virtual void write(const string& path, const string& data) = 0;
  virtual string read(const string& path) = 0;
};
}  // namespace codefs

#endif  // __FILESYSTEM_H__
