#ifndef __CODEFS_UNIX_SOCKET_HANDLER__
#define __CODEFS_UNIX_SOCKET_HANDLER__

#include "SocketHandler.hpp"

namespace codefs {
class UnixSocketHandler : public SocketHandler {
 public:
  UnixSocketHandler();
  virtual bool hasData(int fd);
  virtual ssize_t read(int fd, void* buf, size_t count);
  virtual ssize_t write(int fd, const void* buf, size_t count);
  virtual int connect(const std::string& hostname, int port);
  virtual void listen(int port);
  virtual set<int> getPortFds(int port);
  virtual int accept(int fd);
  virtual void stopListening(int port);
  virtual void close(int fd);

 protected:
  void createServerSockets(int port);
  bool initSocket(int fd);

  set<int> activeSockets;
  map<int, set<int>> portServerSockets;
  recursive_mutex mutex;
};
}  // namespace codefs

#endif  // __CODEFS_UNIX_SOCKET_HANDLER__
