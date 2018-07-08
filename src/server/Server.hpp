#ifndef __CODEFS_SERVER_HPP__
#define __CODEFS_SERVER_HPP__

#include "Headers.hpp"

#include "Scanner.hpp"
#include "ServerFileSystem.hpp"
#include "SocketHandler.hpp"

namespace codefs {
class Server {
 public:
  Server(shared_ptr<SocketHandler> _socketHandler, int _port,
         shared_ptr<ServerFileSystem> _fileSystem);
  void init();
  int update();

 protected:
  shared_ptr<SocketHandler> socketHandler;
  int port;
  shared_ptr<ServerFileSystem> fileSystem;
  int clientFd;
  Scanner scanner;
};
}  // namespace codefs

#endif  // __CODEFS_SERVER_HPP__