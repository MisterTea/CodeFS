#include "Server.hpp"

#include "LogHandler.hpp"
#include "ServerFileSystem.hpp"

namespace {
boost::filesystem::path ROOT_PATH;
shared_ptr<codefs::ServerFileSystem> globalFileSystem;
}  // namespace

namespace codefs {

void runFsWatch() {
  fsw::FSW_EVENT_CALLBACK *cb = [](const std::vector<fsw::event> &events,
                                   void *context) {
    for (const auto &it : events) {
      if (it.get_path().find(ROOT_PATH.string()) != 0) {
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
            LOGFATAL << "Overflow";
          default:
            LOGFATAL << "Unhandled flag " << it2;
        }
      }
    }
  };

  // Create the default platform monitor
  fsw::monitor *active_monitor = fsw::monitor_factory::create_monitor(
      fsw_monitor_type::system_default_monitor_type, {ROOT_PATH.string()}, cb);

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
  // Setup easylogging configurations
  el::Configurations defaultConf = LogHandler::setupLogHandler(&argc, &argv);

  // Parse command line arguments
  cxxopts::Options options("et", "Remote shell for the busy and impatient");

  try {
    options.allow_unrecognised_options();

    options.add_options()         //
        ("h,help", "Print help")  //
        ("port", "Port to listen on",
         cxxopts::value<int>()->default_value("2298"))  //
        ("path", "Absolute path containing code for codefs to monitor",
         cxxopts::value<std::string>()->default_value(""))  //
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
    string maxlogsize = "20971520";
    codefs::LogHandler::setupLogFile(&defaultConf, "/tmp/codefs_server.log",
                                     maxlogsize);

    // Reconfigure default logger to apply settings above
    el::Loggers::reconfigureLogger("default", defaultConf);

    if (!result.count("path")) {
      LOGFATAL << "Please specify a --path flag containing the code path";
    }

    ROOT_PATH = boost::filesystem::canonical(
                    boost::filesystem::path(result["path"].as<string>()))
                    .string();
    cout << "CANONICAL PATH: " << ROOT_PATH << endl;

    // Check for .codefs config
    auto cfgPath = ROOT_PATH / boost::filesystem::path(".codefs");
    set<boost::filesystem::path> excludes;
    if (boost::filesystem::exists(cfgPath)) {
      CSimpleIniA ini(true, true, true);
      SI_Error rc = ini.LoadFile(cfgPath.string().c_str());
      if (rc == 0) {
        auto relativeExcludes =
            split(ini.GetValue("Scanner", "Excludes", NULL), ',');
        for (auto exclude : relativeExcludes) {
          excludes.insert(boost::filesystem::path(ROOT_PATH) / exclude);
        }
      } else {
        LOGFATAL << "Invalid ini file: " << cfgPath;
      }
    }

    // Check for .watchmanconfig and update excludes
    auto pathToCheck = boost::filesystem::path(ROOT_PATH);
    while (true) {
      auto cfgPath = pathToCheck / boost::filesystem::path(".watchmanconfig");
      if (boost::filesystem::exists(cfgPath)) {
        LOG(INFO) << "Found watchman config: " << cfgPath;
        auto configJson = json::parse(fileToStr(cfgPath.string()));
        for (auto ignoreDir : configJson["ignore_dirs"]) {
          auto absoluteIgnoreDir = pathToCheck / ignoreDir.get<std::string>();
          LOG(INFO) << "Adding exclude: " << absoluteIgnoreDir;
          excludes.insert(absoluteIgnoreDir);
        }
        break;
      }
      if (pathToCheck == "/") {
        break;
      }
      pathToCheck = pathToCheck.parent_path();
    }

    shared_ptr<ServerFileSystem> fileSystem(
        new ServerFileSystem(ROOT_PATH.string(), excludes));
    shared_ptr<Server> server(
        new Server(string("tcp://") + "0.0.0.0" + ":" +
                       to_string(result["port"].as<int>()),
                   fileSystem));

    globalFileSystem = fileSystem;
    shared_ptr<thread> watchThread(new thread(runFsWatch));
    usleep(100 * 1000);

    fileSystem->init();
    LOG(INFO) << "Server filesystem initialized";

    fileSystem->setHandler(server.get());
    server->init();
    usleep(100 * 1000);

    auto lastHeartbeatTime = std::chrono::high_resolution_clock::now();
    while (true) {
      int retval = server->update();
      if (retval) {
        return retval;
      }
      auto msSinceLastHeartbeat =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::high_resolution_clock::now() - lastHeartbeatTime)
              .count();
      if (msSinceLastHeartbeat >= 3000) {
        server->heartbeat();
        lastHeartbeatTime = std::chrono::high_resolution_clock::now();
      }
      usleep(1);
    }
  } catch (cxxopts::OptionException &oe) {
    cout << "Exception: " << oe.what() << "\n" << endl;
    cout << options.help({}) << endl;
    exit(1);
  }
}
}  // namespace codefs

int main(int argc, char *argv[]) { codefs::main(argc, argv); }
