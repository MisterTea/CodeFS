#include "Server.hpp"

#include "LogHandler.hpp"
#include "ServerFileSystem.hpp"

DEFINE_int32(port, 2298, "Port to listen on");
DEFINE_string(path, "", "Absolute path containing code for codefs to monitor");
DEFINE_bool(verbose, false, "Verbose logging");
DEFINE_bool(logtostdout, false, "Log to stdout in addition to the log file");

namespace {
shared_ptr<codefs::ServerFileSystem> globalFileSystem;
}

namespace codefs {

void runFsWatch() {
  fsw::FSW_EVENT_CALLBACK *cb = [](const std::vector<fsw::event> &events,
                                   void *context) {
    for (const auto &it : events) {
      if (it.get_path().find(FLAGS_path) != 0) {
        LOG(ERROR) << "FSWatch event on invalid path: " << it.get_path();
        continue;
      }
      for (const auto &it2 : it.get_flags()) {
        switch (it2) {
          case NoOp:
            break;
          case Updated:
          case Link:
          case OwnerModified:
          case AttributeModified:
            LOG(INFO) << it.get_path() << " " << it.get_time()
                      << fsw_get_event_flag_name(it2);
            globalFileSystem->rescanPathAndParent(it.get_path());
            break;
          case Removed:
          case Renamed:
          case MovedFrom:
          case MovedTo:
          case Created:
            LOG(INFO) << it.get_path() << " " << it.get_time()
                      << fsw_get_event_flag_name(it2);
            globalFileSystem->rescanPathAndParentAndChildren(it.get_path());
            break;
          case IsFile:
          case IsDir:
          case IsSymLink:
          case PlatformSpecific:
            break;
          case Overflow:
            LOG(FATAL) << "Overflow";
          default:
            LOG(FATAL) << "Unhandled flag " << it2;
        }
      }
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
  // active_monitor->set_follow_symlinks(true);
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
  defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput,
                          FLAGS_logtostdout ? "true" : "false");
  if (FLAGS_verbose) el::Loggers::setVerboseLevel(3);
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

  // Check for .codefs config
  auto cfgPath =
      boost::filesystem::path(FLAGS_path) / boost::filesystem::path(".codefs");
  set<boost::filesystem::path> excludes;
  if (boost::filesystem::exists(cfgPath)) {
    CSimpleIniA ini(true, true, true);
    SI_Error rc = ini.LoadFile(cfgPath.string().c_str());
    if (rc == 0) {
      auto relativeExcludes =
          split(ini.GetValue("Scanner", "Excludes", NULL), ',');
      for (auto exclude : relativeExcludes) {
        excludes.insert(boost::filesystem::path(FLAGS_path) / exclude);
      }
    } else {
      LOG(FATAL) << "Invalid ini file: " << cfgPath;
    }
  }

  // Check for .watchmanconfig and update excludes
  auto pathToCheck = boost::filesystem::path(FLAGS_path);
  while (true) {
    auto cfgPath = pathToCheck / boost::filesystem::path(".watchmanconfig");
    if (boost::filesystem::exists(cfgPath)) {
      auto configJson = json::parse(fileToStr(cfgPath.string()));
      for (auto ignoreDir : configJson["ignore_dirs"]) {
        excludes.insert(pathToCheck / ignoreDir.get<std::string>());
      }
      break;
    }
    pathToCheck = pathToCheck.parent_path();
  }

  shared_ptr<ServerFileSystem> fileSystem(
      new ServerFileSystem(FLAGS_path, excludes));
  shared_ptr<Server> server(new Server(
      string("tcp://") + "0.0.0.0" + ":" + to_string(FLAGS_port), fileSystem));

  globalFileSystem = fileSystem;
  shared_ptr<thread> watchThread(new thread(runFsWatch));
  usleep(100 * 1000);

  fileSystem->init();
  LOG(INFO) << "Server filesystem initialized";

  fileSystem->setHandler(server.get());
  server->init();
  usleep(100 * 1000);

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
