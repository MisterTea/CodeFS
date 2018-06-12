#include "Client.hpp"

#include "RawSocketUtils.hpp"

namespace codefs {
Client::Client(shared_ptr<SocketHandler> _socketHandler, string _hostname,
               int _port, shared_ptr<FileSystem> _fileSystem)
    : socketHandler(_socketHandler),
      hostname(_hostname),
      port(_port),
      fileSystem(_fileSystem),
      serverFd(-1) {
  serverFd = socketHandler->connect(hostname, port);
}

int Client::update() {
  timeval tv;
  fd_set rfds;
  int numCoreFds = 0;
  int maxCoreFd = 0;
  FD_ZERO(&rfds);
  if (serverFd >= 0) {
    maxCoreFd = max(maxCoreFd, serverFd);
    numCoreFds++;
  }

  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  int numFdsSet = select(maxCoreFd + 1, &rfds, NULL, NULL, &tv);
  FATAL_FAIL(numFdsSet);
  if (numFdsSet == 0) {
    return 0;
  }

  if (FD_ISSET(serverFd, &rfds)) {
    // Got a message from a server
    unsigned char header;
    RawSocketUtils::readAll(serverFd, (char*)&header, 1);
    // TODO: update heartbeat watchdog
    switch (header) {
      // Requests
      case SERVER_CLIENT_HEARTBEAT: {
        RawSocketUtils::writeAll(serverFd, (const char*)&header, 1);
      } break;
      case SERVER_CLIENT_METADATA_UPDATE: {
      } break;

      // Replies
      case CLIENT_SERVER_HEARTBEAT: {
      } break;
      case CLIENT_SERVER_UPDATE_FILE: {
      } break;
      case CLIENT_SERVER_REQUEST_FILE: {
      } break;
    }
  }

  return 0;
}
}  // namespace codefs