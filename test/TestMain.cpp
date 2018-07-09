#include "Headers.hpp"

#include "LogHandler.hpp"

#include "gtest/gtest.h"

DEFINE_int32(v, 0, "verbose level");

namespace codefs {
int main(int argc, char** argv) {
  srand(1);

  // Setup easylogging configurations
  el::Configurations defaultConf = LogHandler::SetupLogHandler(&argc, &argv);
  defaultConf.setGlobally(el::ConfigurationType::ToFile, "false");
  el::Loggers::setVerboseLevel(FLAGS_v);

  // Reconfigure default logger to apply settings above
  el::Loggers::reconfigureLogger("default", defaultConf);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
}  // namespace codefs

int main(int argc, char** argv) { return codefs::main(argc, argv); }
