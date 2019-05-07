#ifndef __TIME_HANDLER_H__
#define __TIME_HANDLER_H__

#include "Headers.hpp"

namespace codefs {
class TimeHandler {
 public:
  static int64_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - initialTime)
        .count();
  }

  static int64_t currentTimeMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now() - initialTime)
        .count();
  }

  static std::chrono::time_point<std::chrono::high_resolution_clock>
      initialTime;
};
}  // namespace codefs

#endif
