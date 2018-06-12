#include "Server.hpp"

#include "ServerFileSystem.hpp"
#include "UnixSocketHandler.hpp"
#include "LogHandler.hpp"
#include "Scanner.hpp"

DEFINE_int32(port, 2298, "Port to listen on");
DEFINE_string(path, "", "Absolute path containing code for codefs to monitor");

namespace codefs {
int main(int argc, char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  srand(1);

  // Setup easylogging configurations
  el::Configurations defaultConf =
      codefs::LogHandler::SetupLogHandler(&argc, &argv);
  defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "false");
  el::Loggers::setVerboseLevel(3);
  // default max log file size is 20MB for etserver
  string maxlogsize = "20971520";
  codefs::LogHandler::SetupLogFile(&defaultConf, "/tmp/codefs_server.log",
                               maxlogsize);

  // Reconfigure default logger to apply settings above
  el::Loggers::reconfigureLogger("default", defaultConf);

  if (!FLAGS_path.length()) {
    LOG(FATAL) << "Please specify a --path flag containing the code path";
  }

  // For now, just scan and then exit
  Scanner scanner;
  scanner.scanRecursively(FLAGS_path);
  exit(0);

  shared_ptr<SocketHandler> socketHandler(new UnixSocketHandler());
  shared_ptr<FileSystem> fileSystem(new ServerFileSystem(FLAGS_path));
  Server server(socketHandler, FLAGS_port, fileSystem);
  fileSystem->setCallback(&server);
  fileSystem->startFuse();
  while (true) {
    int retval = server.update();
    if (retval) {
      return retval;
    }
    usleep(1000);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
