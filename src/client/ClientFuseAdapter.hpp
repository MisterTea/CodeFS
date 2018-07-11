#ifndef __CLIENT_FUSE_ADAPTER_H__
#define __CLIENT_FUSE_ADAPTER_H__

#include "Headers.hpp"

#include "FuseAdapter.hpp"

namespace codefs {
class ClientFileSystem;
class Client;

class ClientFuseAdapter : public FuseAdapter {
 public:
  virtual void assignClientCallbacks(shared_ptr<ClientFileSystem> _fileSystem,
                                     shared_ptr<Client> _client,
                                     fuse_operations* ops);
};
}  // namespace codefs

#endif  // __CLIENT_FUSE_ADAPTER_H__