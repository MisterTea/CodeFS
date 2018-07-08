#include "Server.hpp"

#include "RawSocketUtils.hpp"

namespace codefs {
Server::Server(shared_ptr<SocketHandler> _socketHandler, int _port,
               shared_ptr<ServerFileSystem> _fileSystem)
    : socketHandler(_socketHandler),
      port(_port),
      fileSystem(_fileSystem),
      clientFd(-1) {}

void Server::init() {
  Scanner::scanRecursively(fileSystem.get(), fileSystem->fuseToAbsolute("/"), &(fileSystem->allFileData));
  socketHandler->listen(port);
}

int Server::update() {
  timeval tv;
  fd_set rfds;
  int numCoreFds = 0;
  int maxCoreFd = 0;
  FD_ZERO(&rfds);
  set<int> serverPortFds = socketHandler->getPortFds(port);
  for (int i : serverPortFds) {
    FD_SET(i, &rfds);
    maxCoreFd = max(maxCoreFd, i);
    numCoreFds++;
  }
  if (clientFd >= 0) {
    maxCoreFd = max(maxCoreFd, clientFd);
    numCoreFds++;
  }

  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  int numFdsSet = select(maxCoreFd + 1, &rfds, NULL, NULL, &tv);
  FATAL_FAIL(numFdsSet);
  if (numFdsSet == 0) {
    return 0;
  }

  if (FD_ISSET(clientFd, &rfds)) {
    // Got a message from a client
    unsigned char header;
    RawSocketUtils::readAll(clientFd, (char *)&header, 1);
    // TODO: update heartbeat watchdog
    switch (header) {
      // Requests
      case CLIENT_SERVER_HEARTBEAT: {
        RawSocketUtils::writeAll(clientFd, (const char *)&header, 1);
      } break;
      case CLIENT_SERVER_UPDATE_FILE: {
        FilePathAndContents fpc =
            RawSocketUtils::readProto<FilePathAndContents>(clientFd);
        //fileSystem->write(fpc.path(), fpc.contents());
        RawSocketUtils::writeAll(clientFd, (const char *)&header, 1);
      } break;
      case CLIENT_SERVER_REQUEST_FILE: {
        string path = RawSocketUtils::readMessage(clientFd);
        FilePathAndContents fpc;
        fpc.set_path(path);
        //fpc.set_contents(fileSystem->read(path));
        RawSocketUtils::writeAll(clientFd, (const char *)&header, 1);
        RawSocketUtils::writeProto(clientFd, fpc);
      } break;
      case CLIENT_SERVER_INIT: {
        FilesystemData fsDataProto;
        for (auto &it : fileSystem->allFileData) {
          *(fsDataProto.add_nodes()) = it.second;
        }
        RawSocketUtils::writeAll(clientFd, (const char *)&header, 1);
        RawSocketUtils::writeProto(clientFd, fsDataProto);
      } break;

      // Replies
      case SERVER_CLIENT_HEARTBEAT: {
      } break;
      case SERVER_CLIENT_METADATA_UPDATE: {
      } break;

      default:
       LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  // We have something to do!
  for (int i : serverPortFds) {
    if (FD_ISSET(i, &rfds)) {
      int newClientFd = socketHandler->accept(i);
      // Kill the old client if it exists
      if (clientFd >= 0) {
        socketHandler->close(clientFd);
      }
      clientFd = newClientFd;
      // TODO: Send the initial state
    }
  }

  return 0;
}

}  // namespace codefs