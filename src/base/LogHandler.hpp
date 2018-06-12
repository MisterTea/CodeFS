#ifndef __CODEFS_LOG_HANDLER__
#define __CODEFS_LOG_HANDLER__

#include "Headers.hpp"

namespace codefs {
class LogHandler {
 public:
  static el::Configurations SetupLogHandler(int *argc, char ***argv);
  static void SetupLogFile(el::Configurations *defaultConf, string filename,
                           string maxlogsize = "20971520");
  static void rolloutHandler(const char *filename, std::size_t size);
};
}  // namespace codefs
#endif  // __CODEFS_LOG_HANDLER__
