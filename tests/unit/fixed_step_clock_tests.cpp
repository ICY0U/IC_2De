#include "ic2d/core/fixed_step_clock.hpp"
#include "ic2d/core/log.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
[[nodiscard]] bool near(const double left, const double right, const double epsilon = 1.0e-9) {
    return std::abs(left - right) <= epsilon;
}

void test_accumulates_partial_ticks() {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step};

    const auto first = clock.advance(fixed_step * 0.5);
    expect(first.fixed_steps == 0U, "Half a fixed step must not simulate early.");
    expect(near(first.interpolation_alpha, 0.5), "Half a fixed step must interpolate at 0.5.");

    const auto second = clock.advance(fixed_step * 0.5);
    expect(second.fixed_steps == 1U, "Two half steps must produce one fixed update.");
    expect(near(second.interpolation_alpha, 0.0), "A complete fixed step must leave no remainder.");
}

void test_bounds_catch_up_work() {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step, 0.25, 4};

    const auto plan = clock.advance(1.0);
    expect(plan.fixed_steps == 4U, "A stalled frame must respect the fixed-step work limit.");
    expect(plan.dropped_time, "A stalled frame must report that time was dropped.");
    expect(plan.interpolation_alpha >= 0.0 && plan.interpolation_alpha <= 1.0,
           "Interpolation alpha must stay normalized.");
}

void test_ignores_invalid_frame_delta() {
    ic2d::FixedStepClock clock{1.0 / 60.0};
    const auto plan = clock.advance(-1.0);
    expect(plan.fixed_steps == 0U, "A negative frame delta must not update simulation.");
    expect(!plan.dropped_time, "An ignored frame delta must not report dropped time.");
}

void test_reset_discards_accumulated_time() {
    constexpr double fixed_step = 1.0 / 60.0;
    ic2d::FixedStepClock clock{fixed_step};
    static_cast<void>(clock.advance(fixed_step * 0.5));
    clock.reset();

    const auto plan = clock.advance(fixed_step * 0.5);
    expect(plan.fixed_steps == 0U, "Reset must discard previously accumulated time.");
    expect(near(plan.interpolation_alpha, 0.5), "Clock must restart from an empty accumulator.");
}

void test_rejects_invalid_configuration() {
    bool threw = false;
    try {
        const ic2d::FixedStepClock invalid_clock{0.0};
        static_cast<void>(invalid_clock);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "A zero fixed step must be rejected.");
}

} // namespace

int run_automated_run_tests();

int main() {
    failures += run_automated_run_tests();
    test_accumulates_partial_ticks();
    test_bounds_catch_up_work();
    test_ignores_invalid_frame_delta();
    test_reset_discards_accumulated_time();
    test_rejects_invalid_configuration();

    if (failures == 0) {
        ic2d::log(ic2d::LogLevel::info, "All fixed-step clock tests passed.");
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
