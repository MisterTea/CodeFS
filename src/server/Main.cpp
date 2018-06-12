#include "Server.hpp"

#include "UnixSocketHandler.hpp"
#include "ServerFileSystem.hpp"

DEFINE_int32(port, 2298, "Port to listen on");

namespace codefs {
    int main(int argc, char *argv[]) {
        shared_ptr<SocketHandler> socketHandler(new UnixSocketHandler());
        shared_ptr<FileSystem> fileSystem(new ServerFileSystem());
        Server server(socketHandler, FLAGS_port, fileSystem);
        while(true) {
            int retval = server.update();
            if (retval) {
                return retval;
            }
            usleep(1000);
        }
    }
}

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
