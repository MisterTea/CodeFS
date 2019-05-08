#ifndef __CODEFS_LOG_HANDLER__
#define __CODEFS_LOG_HANDLER__

#include "Headers.hpp"

namespace codefs {
class LogHandler {
 public:
  static el::Configurations setupLogHandler(int *argc, char ***argv);
  static void setupLogFile(el::Configurations *defaultConf, string filename,
                           string maxlogsize = "20971520");
  static void rolloutHandler(const char *filename, std::size_t size);
  static string stderrToFile(const string &pathPrefix);
};
}  // namespace codefs
#endif  // __CODEFS_LOG_HANDLER__
