#include "Server.hpp"

#include "LogHandler.hpp"
#include "Scanner.hpp"
#include "ServerFileSystem.hpp"

DEFINE_int32(port, 2298, "Port to listen on");
DEFINE_string(path, "", "Absolute path containing code for codefs to monitor");

namespace {
shared_ptr<codefs::ServerFileSystem> globalFileSystem;
}

namespace codefs {

void runFsWatch() {
  fsw::FSW_EVENT_CALLBACK *cb = [](const std::vector<fsw::event> &events,
                                   void *context) {
    cout << "GOT EVENT: " << events.size() << endl;
    for (const auto &it : events) {
      cout << it.get_path() << " " << it.get_time() << " (";
      for (const auto &it2 : it.get_flags()) {
        cout << fsw_get_event_flag_name(it2) << ", ";
        switch (it2) {
          case NoOp:
            break;
          case PlatformSpecific:
            break;
          case Updated:
          case Link:
          case OwnerModified:
          case AttributeModified:
            globalFileSystem->rescan(it.get_path());
            break;
          case Removed:
          case Renamed:
          case MovedFrom:
          case MovedTo:
          case Created:
            globalFileSystem->rescanPathAndParent(it.get_path());
            break;
          case IsFile:
          case IsDir:
          case IsSymLink:
            break;
          case Overflow:
            LOG(FATAL) << "Overflow";
        }
      }
      cout << ")" << endl;
    }
  };

  // Create the default platform monitor
  fsw::monitor *active_monitor = fsw::monitor_factory::create_monitor(
      fsw_monitor_type::system_default_monitor_type, {FLAGS_path}, cb);

  // Configure the monitor
  // active_monitor->set_properties(monitor_properties);
  active_monitor->set_allow_overflow(true);
  // active_monitor->set_latency(latency);
  active_monitor->set_recursive(true);
  // active_monitor->set_directory_only(directory_only);
  // active_monitor->set_event_type_filters(event_filters);
  // active_monitor->set_filters(filters);
  active_monitor->set_follow_symlinks(true);
  // active_monitor->set_watch_access(watch_access);

  // Start the monitor
  active_monitor->start();
}

int main(int argc, char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  srand(1);

  // Setup easylogging configurations
  el::Configurations defaultConf =
      codefs::LogHandler::SetupLogHandler(&argc, &argv);
  defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "true");
  el::Loggers::setVerboseLevel(3);
  string maxlogsize = "20971520";
  codefs::LogHandler::SetupLogFile(&defaultConf, "/tmp/codefs_server.log",
                                   maxlogsize);

  // Reconfigure default logger to apply settings above
  el::Loggers::reconfigureLogger("default", defaultConf);

  if (!FLAGS_path.length()) {
    LOG(FATAL) << "Please specify a --path flag containing the code path";
  }

  FLAGS_path = boost::filesystem::canonical(boost::filesystem::path(FLAGS_path))
                   .string();
  cout << "CANONICAL PATH: " << FLAGS_path << endl;

  shared_ptr<ServerFileSystem> fileSystem(new ServerFileSystem(FLAGS_path));
  shared_ptr<Server> server(new Server(
      string("tcp://") + "0.0.0.0" + ":" + to_string(FLAGS_port), fileSystem));
  fileSystem->setHandler(server.get());
  server->init();
  usleep(100 * 1000);

  globalFileSystem = fileSystem;
  shared_ptr<thread> watchThread(new thread(runFsWatch));
  usleep(100 * 1000);

  fileSystem->init();

  int counter = 0;
  while (true) {
    int retval = server->update();
    if (retval) {
      return retval;
    }
    if (++counter % 3000 == 0) {
      server->heartbeat();
    }
    usleep(1000);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
