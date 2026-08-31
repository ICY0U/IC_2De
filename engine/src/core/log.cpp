#include "ic2d/core/log.hpp"

#include <iostream>
#include <mutex>

namespace ic2d {
namespace {

[[nodiscard]] const char* label(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace

void log(const LogLevel level, const std::string_view message) {
    static std::mutex output_mutex;
    const std::scoped_lock lock{output_mutex};
    std::clog << "[IC2DE][" << label(level) << "] " << message << '\n';
}

} // namespace ic2d
