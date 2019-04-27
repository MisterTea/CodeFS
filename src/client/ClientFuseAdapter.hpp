#ifndef __CLIENT_FUSE_ADAPTER_H__
#define __CLIENT_FUSE_ADAPTER_H__

#include "Headers.hpp"

namespace codefs {
class ClientFileSystem;
class Client;

class ClientFuseAdapter {
 public:
  virtual void assignCallbacks(shared_ptr<ClientFileSystem> _fileSystem,
                               shared_ptr<Client> _client,
                               fuse_operations* ops);
};
}  // namespace codefs

#endif  // __CLIENT_FUSE_ADAPTER_H__