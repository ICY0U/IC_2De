#pragma once

#include <cstdint>

namespace ic2d {

struct TickPlan {
    std::uint32_t fixed_steps{0};
    double interpolation_alpha{0.0};
    bool dropped_time{false};
};

class FixedStepClock final {
public:
    explicit FixedStepClock(double fixed_step_seconds, double max_frame_seconds = 0.25,
                            std::uint32_t max_steps_per_frame = 8);

    [[nodiscard]] TickPlan advance(double frame_seconds) noexcept;
    void reset() noexcept;

private:
    double fixed_step_seconds_;
    double max_frame_seconds_;
    std::uint32_t max_steps_per_frame_;
    double accumulator_seconds_{0.0};
};

} // namespace ic2d
