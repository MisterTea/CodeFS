#ifndef __SERVER_FUSE_ADAPTER_H__
#define __SERVER_FUSE_ADAPTER_H__

#include "Headers.hpp"

#include "FuseAdapter.hpp"

namespace codefs {
class ServerFileSystem;

class ServerFuseAdapter : public FuseAdapter {
 public:
  virtual void assignServerCallbacks(shared_ptr<ServerFileSystem> _fileSystem,
                               fuse_operations* ops);
};
}  // namespace codefs

#endif  // __SERVER_FUSE_ADAPTER_H__