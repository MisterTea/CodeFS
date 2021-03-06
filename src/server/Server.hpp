#ifndef __CODEFS_SERVER_HPP__
#define __CODEFS_SERVER_HPP__

#include "Headers.hpp"

#include "MessageReader.hpp"
#include "MessageWriter.hpp"
#include "ServerFileSystem.hpp"
#include "ZmqBiDirectionalRpc.hpp"

namespace codefs {
class Server : public ServerFileSystem::Handler {
 public:
  Server(const string& address, shared_ptr<ServerFileSystem> _fileSystem);

  virtual ~Server() {}

  void init();
  int update();
  inline void heartbeat() { rpc->heartbeat(); }

  virtual void metadataUpdated(const string& path, const FileData& fileData);

 protected:
  RpcId request(const string& payload) {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    return rpc->request(payload);
  }
  void reply(const RpcId& rpcId, const string& payload) {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    rpc->reply(rpcId, payload);
  }

  string address;
  shared_ptr<ZmqBiDirectionalRpc> rpc;
  int port;
  shared_ptr<ServerFileSystem> fileSystem;
  int clientFd;
  recursive_mutex rpcMutex;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_HPP__