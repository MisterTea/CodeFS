#include "Client.hpp"

#include "ClientFileSystem.hpp"
#include "ClientFuseAdapter.hpp"
#include "LogHandler.hpp"

DEFINE_string(hostname, "localhost", "Hostname to connect to");
DEFINE_int32(port, 2298, "Port to connect to");
DEFINE_string(mountpoint, "/tmp/clientmount",
              "Where to mount the FS for server access");

namespace codefs {
struct loopback {};

static struct loopback loopback;

static const struct fuse_opt codefs_opts[] = {
    // { "case_insensitive", offsetof(struct loopback, case_insensitive), 1 },
    FUSE_OPT_END};

void runFuse(char *binaryLocation, shared_ptr<Client> client,
             shared_ptr<ClientFileSystem> fileSystem) {
  int argc = 4;
  const char *const_argv[] = {binaryLocation, FLAGS_mountpoint.c_str(), "-d",
                              "-s"};
  char **argv = (char **)const_argv;
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  if (fuse_opt_parse(&args, &loopback, codefs_opts, NULL) == -1) {
    exit(1);
  }

  umask(0);
  fuse_operations codefs_oper;
  memset(&codefs_oper, 0, sizeof(fuse_operations));

  ClientFuseAdapter adapter;
  adapter.assignClientCallbacks(fileSystem, client, &codefs_oper);

  int res = fuse_main(argc, argv, &codefs_oper, NULL);
  fuse_opt_free_args(&args);
  if (res) {
    LOG(FATAL) << "Unclean exit from fuse thread: " << res
               << " (errno: " << errno << ")";
  }
}

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
  codefs::LogHandler::SetupLogFile(&defaultConf, "/tmp/codefs_client.log",
                                   maxlogsize);

  // Reconfigure default logger to apply settings above
  el::Loggers::reconfigureLogger("default", defaultConf);

  shared_ptr<ClientFileSystem> fileSystem(
      new ClientFileSystem(FLAGS_mountpoint));
  shared_ptr<Client> client(new Client(
      string("tcp://") + FLAGS_hostname + ":" + to_string(FLAGS_port),
      fileSystem));
  sleep(1);

  shared_ptr<thread> fuseThread(
      new thread(runFuse, argv[0], client, fileSystem));

  int counter = 0;
  while (true) {
    int retval = client->update();
    if (retval) {
      return retval;
    }
    if (++counter % 3000 == 0) {
      client->heartbeat();
    }
    usleep(1000);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
