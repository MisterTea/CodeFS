#include "TimeHandler.hpp"

namespace codefs {
std::chrono::time_point<std::chrono::system_clock> TimeHandler::initialTime =
    std::chrono::system_clock::now();
}