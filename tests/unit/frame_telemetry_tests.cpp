#include "ic2d/core/frame_telemetry.hpp"

#include <cmath>
#include <iostream>
#include <limits>
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

[[nodiscard]] bool near(const double left, const double right) {
    return std::abs(left - right) < 0.0001;
}

void test_percentiles_are_computed_in_milliseconds() {
    ic2d::FrameTimeSeries series{5};
    expect(series.record_seconds(0.001), "A finite frame sample must be accepted.");
    expect(series.record_seconds(0.002), "A second finite frame sample must be accepted.");
    expect(series.record_seconds(0.003), "A third finite frame sample must be accepted.");
    expect(series.record_seconds(0.004), "A fourth finite frame sample must be accepted.");
    expect(series.record_seconds(0.005), "A fifth finite frame sample must be accepted.");

    const auto summary = series.summary();
    expect(summary.sample_count == 5U, "The summary must report every retained sample.");
    expect(near(summary.latest_milliseconds, 5.0), "Latest time must use milliseconds.");
    expect(near(summary.mean_milliseconds, 3.0), "Mean time must use all samples.");
    expect(near(summary.p50_milliseconds, 3.0), "p50 must use nearest-rank selection.");
    expect(near(summary.p95_milliseconds, 5.0), "p95 must select the slow tail.");
    expect(near(summary.p99_milliseconds, 5.0), "p99 must select the slowest sample here.");
}

void test_window_overwrites_oldest_samples() {
    ic2d::FrameTimeSeries series{3};
    static_cast<void>(series.record_seconds(0.010));
    static_cast<void>(series.record_seconds(0.020));
    static_cast<void>(series.record_seconds(0.030));
    static_cast<void>(series.record_seconds(0.040));

    const auto summary = series.summary();
    expect(summary.sample_count == 3U, "A full window must retain only its capacity.");
    expect(near(summary.mean_milliseconds, 30.0), "The overwritten sample must leave the mean.");
    expect(near(summary.latest_milliseconds, 40.0), "Latest must survive ring wraparound.");
}

void test_invalid_samples_and_configuration_fail_safely() {
    ic2d::FrameTimeSeries series{2};
    expect(!series.record_seconds(-0.1), "Negative frame time must be rejected.");
    expect(!series.record_seconds(std::numeric_limits<double>::infinity()),
           "Infinite frame time must be rejected.");
    expect(series.summary().sample_count == 0U,
           "Rejected samples must not mutate telemetry state.");

    bool threw = false;
    try {
        const ic2d::FrameTimeSeries invalid{0};
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "A zero-capacity telemetry window must be rejected.");
}

void test_clear_keeps_capacity_and_discards_history() {
    ic2d::FrameTimeSeries series{4};
    static_cast<void>(series.record_seconds(0.016));
    series.clear();
    expect(series.capacity() == 4U, "Clear must preserve preallocated storage.");
    expect(series.summary().sample_count == 0U, "Clear must discard prior samples.");
}

} // namespace

int main() {
    test_percentiles_are_computed_in_milliseconds();
    test_window_overwrites_oldest_samples();
    test_invalid_samples_and_configuration_fail_safely();
    test_clear_keeps_capacity_and_discards_history();

    if (failures == 0) {
        std::cout << "All frame telemetry tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
