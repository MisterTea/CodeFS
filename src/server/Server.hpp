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
  virtual void metadataUpdated(const string &path,
                               const FileData &fileData);

 protected:
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