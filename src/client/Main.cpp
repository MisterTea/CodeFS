#include "Client.hpp"

#include "UnixSocketHandler.hpp"
#include "ClientFileSystem.hpp"

DEFINE_string(hostname, "localhost", "Hostname to connect to");
DEFINE_int32(port, 2298, "Port to connect to");

namespace codefs {
int main(int argc, char *argv[]) {
  shared_ptr<SocketHandler> socketHandler(new UnixSocketHandler());
  shared_ptr<FileSystem> fileSystem(new ClientFileSystem());
  Client client(socketHandler, FLAGS_hostname, FLAGS_port, fileSystem);
  while (true) {
    int retval = client.update();
    if (retval) {
      return retval;
    }
    usleep(1000);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
