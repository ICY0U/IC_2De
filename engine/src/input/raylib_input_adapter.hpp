#pragma once

#include "ic2d/input.hpp"

namespace ic2d {

class RaylibInputAdapter final {
public:
    [[nodiscard]] InputSample sample() const noexcept;
};

} // namespace ic2d
