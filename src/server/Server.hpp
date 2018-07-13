#ifndef __CODEFS_SERVER_HPP__
#define __CODEFS_SERVER_HPP__

#include "Headers.hpp"

#include "BiDirectionalRpc.hpp"
#include "MessageReader.hpp"
#include "MessageWriter.hpp"
#include "Scanner.hpp"
#include "ServerFileSystem.hpp"

namespace codefs {
class Server : public ServerFileSystem::Handler {
 public:
  Server(const string &address, shared_ptr<ServerFileSystem> _fileSystem);
  void init();
  int update();
  inline void heartbeat() { rpc->heartbeat(); }

  virtual void metadataUpdated(const string &path,
                               const FileData &fileData);

 protected:
  RpcId request(const string& payload) {
    lock_guard<mutex> lock(rpcMutex);
    return rpc->request(payload);
  }
  void reply(const RpcId& rpcId, const string& payload) {
    lock_guard<mutex> lock(rpcMutex);
    rpc->reply(rpcId, payload);
  }

  string address;
  shared_ptr<BiDirectionalRpc> rpc;
  int port;
  shared_ptr<ServerFileSystem> fileSystem;
  int clientFd;
  Scanner scanner;
  MessageReader reader;
  MessageWriter writer;
  mutex rpcMutex;
  unordered_set<string> clientLockedPaths;


};
}  // namespace codefs

#endif  // __CODEFS_SERVER_HPP__