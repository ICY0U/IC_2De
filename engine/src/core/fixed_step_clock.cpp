#include "ic2d/core/fixed_step_clock.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ic2d {

FixedStepClock::FixedStepClock(
    const double fixed_step_seconds,
    const double max_frame_seconds,
    const std::uint32_t max_steps_per_frame
)
    : fixed_step_seconds_{fixed_step_seconds},
      max_frame_seconds_{max_frame_seconds},
      max_steps_per_frame_{max_steps_per_frame} {
    if (!std::isfinite(fixed_step_seconds_) || fixed_step_seconds_ <= 0.0) {
        throw std::invalid_argument{"Fixed step must be finite and greater than zero."};
    }
    if (!std::isfinite(max_frame_seconds_) || max_frame_seconds_ <= 0.0) {
        throw std::invalid_argument{"Maximum frame time must be finite and greater than zero."};
    }
    if (max_steps_per_frame_ == 0U) {
        throw std::invalid_argument{"Maximum fixed steps per frame must be greater than zero."};
    }
}

TickPlan FixedStepClock::advance(const double frame_seconds) noexcept {
    if (!std::isfinite(frame_seconds) || frame_seconds <= 0.0) {
        return TickPlan{
            .fixed_steps = 0,
            .interpolation_alpha = std::clamp(accumulator_seconds_ / fixed_step_seconds_, 0.0, 1.0),
            .dropped_time = false,
        };
    }

    const double accepted_frame_seconds = std::min(frame_seconds, max_frame_seconds_);
    accumulator_seconds_ += accepted_frame_seconds;

    auto pending_steps = static_cast<std::uint32_t>(accumulator_seconds_ / fixed_step_seconds_);
    bool dropped_time = frame_seconds > max_frame_seconds_;

    if (pending_steps > max_steps_per_frame_) {
        const auto excess_steps = pending_steps - max_steps_per_frame_;
        accumulator_seconds_ -= static_cast<double>(excess_steps) * fixed_step_seconds_;
        pending_steps = max_steps_per_frame_;
        dropped_time = true;
    }

    accumulator_seconds_ -= static_cast<double>(pending_steps) * fixed_step_seconds_;
    if (accumulator_seconds_ < 0.0) {
        accumulator_seconds_ = 0.0;
    }

    return TickPlan{
        .fixed_steps = pending_steps,
        .interpolation_alpha = std::clamp(accumulator_seconds_ / fixed_step_seconds_, 0.0, 1.0),
        .dropped_time = dropped_time,
    };
}

void FixedStepClock::reset() noexcept {
    accumulator_seconds_ = 0.0;
}

} // namespace ic2d
