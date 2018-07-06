#include "Headers.hpp"

#include "FileSystem.hpp"
#include "SocketHandler.hpp"

namespace codefs {
class Client {
 public:
  Client(shared_ptr<SocketHandler> _socketHandler, string _hostname, int _port,
         shared_ptr<FileSystem> _fileSystem);
  int update();

 protected:
  shared_ptr<SocketHandler> socketHandler;
  string hostname;
  int port;
  shared_ptr<FileSystem> fileSystem;
  int serverFd;
  unordered_map<string, FileData> fsData;
};
}  // namespace codefs