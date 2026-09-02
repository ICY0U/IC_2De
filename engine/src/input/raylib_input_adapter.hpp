#pragma once

#include "ic2d/input.hpp"

namespace ic2d {

class RaylibInputAdapter final {
public:
    [[nodiscard]] InputSample sample() noexcept;

private:
    bool pointer_aim_active_{true};
};

} // namespace ic2d
