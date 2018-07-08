#ifndef __FUSE_ADAPTER_H__
#define __FUSE_ADAPTER_H__

#include "Headers.hpp"

namespace codefs {
class FileSystem;

class FuseAdapter {
 public:
  virtual void assignCallbacks(shared_ptr<FileSystem> _fileSystem,
                               fuse_operations* ops);
};
}  // namespace codefs

#endif  // __FUSE_ADAPTER_H__