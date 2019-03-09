#ifndef __TIME_HANDLER_H__
#define __TIME_HANDLER_H__

#include "Headers.hpp"

namespace codefs {
class TimeHandler {
 public:
  static int64_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now() - initialTime)
        .count();
  }

  static std::chrono::time_point<std::chrono::system_clock> initialTime;
};
}  // namespace codefs

#endif
