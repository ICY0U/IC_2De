#include <doctest/doctest.h>

#include "ic2d/core/fixed_step_clock.hpp"
#include "ic2d/core/log.hpp"

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] bool near(const double left, const double right, const double epsilon = 1.0e-9) {
    return std::abs(left - right) <= epsilon;
}

TEST_CASE("accumulates partial ticks") {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step};

    const auto first = clock.advance(fixed_step * 0.5);
    CHECK_MESSAGE((first.fixed_steps == 0U), "Half a fixed step must not simulate early.");
    CHECK_MESSAGE((near(first.interpolation_alpha, 0.5)),
                  "Half a fixed step must interpolate at 0.5.");

    const auto second = clock.advance(fixed_step * 0.5);
    CHECK_MESSAGE((second.fixed_steps == 1U), "Two half steps must produce one fixed update.");
    CHECK_MESSAGE((near(second.interpolation_alpha, 0.0)),
                  "A complete fixed step must leave no remainder.");
}

TEST_CASE("bounds catch up work") {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step, 0.25, 4};

    const auto plan = clock.advance(1.0);
    CHECK_MESSAGE((plan.fixed_steps == 4U),
                  "A stalled frame must respect the fixed-step work limit.");
    CHECK_MESSAGE((plan.dropped_time), "A stalled frame must report that time was dropped.");
    CHECK_MESSAGE((plan.interpolation_alpha >= 0.0 && plan.interpolation_alpha <= 1.0),
                  "Interpolation alpha must stay normalized.");
}

TEST_CASE("ignores invalid frame delta") {
    ic2d::FixedStepClock clock{1.0 / 60.0};
    const auto plan = clock.advance(-1.0);
    CHECK_MESSAGE((plan.fixed_steps == 0U), "A negative frame delta must not update simulation.");
    CHECK_MESSAGE((!plan.dropped_time), "An ignored frame delta must not report dropped time.");
}

TEST_CASE("reset discards accumulated time") {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step};
    static_cast<void>(clock.advance(fixed_step * 0.5));
    clock.reset();

    const auto plan = clock.advance(fixed_step * 0.5);
    CHECK_MESSAGE((plan.fixed_steps == 0U), "Reset must discard previously accumulated time.");
    CHECK_MESSAGE((near(plan.interpolation_alpha, 0.5)),
                  "Clock must restart from an empty accumulator.");
}

TEST_CASE("rejects invalid configuration") {
    bool threw = false;
    try {
        const ic2d::FixedStepClock invalid_clock{0.0};
        static_cast<void>(invalid_clock);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "A zero fixed step must be rejected.");
}

} // namespace

int run_automated_run_tests();
