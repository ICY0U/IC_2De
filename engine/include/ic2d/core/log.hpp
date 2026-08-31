#pragma once

#include <string_view>

namespace ic2d {

enum class LogLevel {
    info,
    warning,
    error,
};

void log(LogLevel level, std::string_view message);

} // namespace ic2d
