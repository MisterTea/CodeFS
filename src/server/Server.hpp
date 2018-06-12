#ifndef __CODEFS_SERVER_HPP__
#define __CODEFS_SERVER_HPP__

#include "Headers.hpp"

#include "FileSystem.hpp"
#include "SocketHandler.hpp"

namespace codefs {
class Server : public FileSystem::FsCallbackHandler {
 public:
  Server(shared_ptr<SocketHandler> _socketHandler, int _port,
         shared_ptr<FileSystem> _fileSystem);
  int update();
  virtual void fileChanged(const string &fusePath, const string &absolutePath);

 protected:
  shared_ptr<SocketHandler> socketHandler;
  int port;
  shared_ptr<FileSystem> fileSystem;
  int clientFd;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_HPP__