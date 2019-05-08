#include "Headers.hpp"

#include "LogHandler.hpp"

#define CATCH_CONFIG_RUNNER
#include "Catch2/single_include/catch2/catch.hpp"

DEFINE_int32(v, 0, "verbose level");

namespace codefs {
int main(int argc, char** argv) {
  srand(1);

  // Setup easylogging configurations
  el::Configurations defaultConf = LogHandler::setupLogHandler(&argc, &argv);
  defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, "true");
  defaultConf.setGlobally(el::ConfigurationType::ToFile, "true");
  // el::Loggers::setVerboseLevel(9);

  string stderrPathPrefix =
      string("/tmp/et_test_") + to_string(rand()) + string("_");
  string stderrPath = LogHandler::stderrToFile(stderrPathPrefix);
  cout << "Writing stderr to " << stderrPath << endl;

  string logDirectoryPattern = string("/tmp/et_test_XXXXXXXX");
  string logDirectory = string(mkdtemp(&logDirectoryPattern[0]));
  string logPath = string(logDirectory) + "/log";
  cout << "Writing log to " << logPath << endl;
  LogHandler::setupLogFile(&defaultConf, logPath);

  // Reconfigure default logger to apply settings above
  el::Loggers::reconfigureLogger("default", defaultConf);

  int result = Catch::Session().run(argc, argv);

  FATAL_FAIL(::remove(stderrPath.c_str()));
  FATAL_FAIL(::remove(logPath.c_str()));
  FATAL_FAIL(::remove(logDirectory.c_str()));
  return result;
}
}  // namespace codefs

int main(int argc, char** argv) { return codefs::main(argc, argv); }
