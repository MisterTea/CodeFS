#include "Client.hpp"

#include "ClientFileSystem.hpp"
#include "ClientFuseAdapter.hpp"
#include "LogHandler.hpp"

namespace codefs {
struct loopback {};

static struct loopback loopback;

static const struct fuse_opt codefs_opts[] = {
    // { "case_insensitive", offsetof(struct loopback, case_insensitive), 1 },
    FUSE_OPT_END};

void runFuse(char *binaryLocation, shared_ptr<Client> client,
             shared_ptr<ClientFileSystem> fileSystem,
             const std::string &mountPoint, bool logToStdout) {
  vector<string> fuseFlags = {binaryLocation, mountPoint.c_str()};  //, "-s"};
#if __APPLE__
  // OSXFUSE has a timeout in the kernel.  Because we can block on network
  // failure, we disable this timeout
  fuseFlags.push_back("-odaemon_timeout=2592000");
  fuseFlags.push_back("-ojail_symlinks");
#endif
  if (logToStdout) {
    fuseFlags.push_back("-d");
  } else {
    fuseFlags.push_back("-f");
  }

  int argc = int(fuseFlags.size());
  char **argv = new char *[argc];
  for (int a = 0; a < argc; a++) {
    argv[a] = &(fuseFlags[a][0]);
  }
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  if (fuse_opt_parse(&args, &loopback, codefs_opts, NULL) == -1) {
    LOGFATAL << "Error parsing fuse options";
  }

  umask(0);
  fuse_operations codefs_oper;
  memset(&codefs_oper, 0, sizeof(fuse_operations));

  ClientFuseAdapter adapter;
  adapter.assignCallbacks(fileSystem, client, &codefs_oper);

  int res = fuse_main(argc, argv, &codefs_oper, NULL);
  fuse_opt_free_args(&args);
  if (res) {
    LOGFATAL << "Unclean exit from fuse thread: " << res << " (errno: " << errno
             << ")";
  } else {
    LOG(INFO) << "FUSE THREAD EXIT";
  }
}  // namespace codefs

int main(int argc, char *argv[]) {
  // Setup easylogging configurations
  el::Configurations defaultConf = LogHandler::setupLogHandler(&argc, &argv);

  // Parse command line arguments
  cxxopts::Options options("et", "Remote shell for the busy and impatient");

  try {
    options.allow_unrecognised_options();

    options.add_options()         //
        ("h,help", "Print help")  //
        ("port", "Port to connect to",
         cxxopts::value<int>()->default_value("2298"))  //
        ("hostname", "Hostname to connect to",
         cxxopts::value<std::string>())  //
        ("mountpoint", "Where to mount the FS for server access",
         cxxopts::value<std::string>()->default_value("/tmp/clientmount"))  //
        ("v,verbose", "Enable verbose logging",
         cxxopts::value<int>()->default_value("0"))  //
        ("logtostdout", "Write log to stdout")       //
        ;

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      cout << options.help({}) << endl;
      exit(0);
    }

    if (result.count("logtostdout")) {
      defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "true");
    } else {
      defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "false");
    }

    if (result.count("verbose")) {
      el::Loggers::setVerboseLevel(result["verbose"].as<int>());
    }
    // default max log file size is 20MB for etserver
    string maxlogsize = "20971520";
    codefs::LogHandler::setupLogFile(&defaultConf, "/tmp/codefs.log",
                                     maxlogsize);

    // Reconfigure default logger to apply settings above
    el::Loggers::reconfigureLogger("default", defaultConf);

    // Create mountpoint if it doesn't exist
    boost::filesystem::path pt(result["mountpoint"].as<string>());
    if (!exists(pt)) {
      boost::filesystem::create_directory(pt);
    } else {
      if (!boost::filesystem::is_directory(pt)) {
        cout << "Error: The mountpoint is not a directory" << endl;
        LOGFATAL << "The mountpoint is not a directory";
      }
    }

    int port = result["port"].as<int>();

    shared_ptr<ClientFileSystem> fileSystem(
        new ClientFileSystem(result["mountpoint"].as<string>()));
    shared_ptr<Client> client(new Client(string("tcp://") +
                                             result["hostname"].as<string>() +
                                             ":" + to_string(port),
                                         fileSystem));
    sleep(1);

    auto future = std::async(std::launch::async, [client] {
      auto lastHeartbeatTime = std::chrono::high_resolution_clock::now();
      while (true) {
        int retval = client->update();
        if (retval) {
          return retval;
        }
        auto msSinceLastHeartbeat =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - lastHeartbeatTime)
                .count();
        if (msSinceLastHeartbeat >= 3000) {
          client->heartbeat();
          lastHeartbeatTime = std::chrono::high_resolution_clock::now();
        }
        usleep(1);
      }
    });

    string mountPoint = result["mountpoint"].as<string>();

    runFuse(argv[0], client, fileSystem, mountPoint,
            result.count("logtostdout") > 0);

    LOG(INFO) << "Client finished";
    cout << "Client finished" << endl;
    return 0;
  } catch (cxxopts::OptionException &oe) {
    cout << "Exception: " << oe.what() << "\n" << endl;
    cout << options.help({}) << endl;
    exit(1);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
