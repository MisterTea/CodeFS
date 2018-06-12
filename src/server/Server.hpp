#include "Headers.hpp"

#include "SocketHandler.hpp"
#include "FileSystem.hpp"

namespace codefs {
class Server {
 public:
  Server(shared_ptr<SocketHandler> _socketHandler, int _port, shared_ptr<FileSystem> _fileSystem);
  int update();

 protected:
  shared_ptr<SocketHandler> socketHandler;
  int port;
  shared_ptr<FileSystem> fileSystem;
  int clientFd;
};
}  // namespace codefs