#include "TimeHandler.hpp"

namespace codefs {
std::chrono::time_point<std::chrono::high_resolution_clock>
    TimeHandler::initialTime = std::chrono::high_resolution_clock::now();
}