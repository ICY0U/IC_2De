#include <doctest/doctest.h>

#include "ic2d/core/frame_telemetry.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] bool near(const double left, const double right) {
    return std::abs(left - right) < 0.0001;
}

TEST_CASE("percentiles are computed in milliseconds") {
    ic2d::FrameTimeSeries series{5};
    CHECK_MESSAGE((series.record_seconds(0.001)), "A finite frame sample must be accepted.");
    CHECK_MESSAGE((series.record_seconds(0.002)), "A second finite frame sample must be accepted.");
    CHECK_MESSAGE((series.record_seconds(0.003)), "A third finite frame sample must be accepted.");
    CHECK_MESSAGE((series.record_seconds(0.004)), "A fourth finite frame sample must be accepted.");
    CHECK_MESSAGE((series.record_seconds(0.005)), "A fifth finite frame sample must be accepted.");

    const auto summary = series.summary();
    CHECK_MESSAGE((summary.sample_count == 5U), "The summary must report every retained sample.");
    CHECK_MESSAGE((near(summary.latest_milliseconds, 5.0)), "Latest time must use milliseconds.");
    CHECK_MESSAGE((near(summary.mean_milliseconds, 3.0)), "Mean time must use all samples.");
    CHECK_MESSAGE((near(summary.p50_milliseconds, 3.0)), "p50 must use nearest-rank selection.");
    CHECK_MESSAGE((near(summary.p95_milliseconds, 5.0)), "p95 must select the slow tail.");
    CHECK_MESSAGE((near(summary.p99_milliseconds, 5.0)),
                  "p99 must select the slowest sample here.");
}

TEST_CASE("window overwrites oldest samples") {
    ic2d::FrameTimeSeries series{3};
    static_cast<void>(series.record_seconds(0.010));
    static_cast<void>(series.record_seconds(0.020));
    static_cast<void>(series.record_seconds(0.030));
    static_cast<void>(series.record_seconds(0.040));

    const auto summary = series.summary();
    CHECK_MESSAGE((summary.sample_count == 3U), "A full window must retain only its capacity.");
    CHECK_MESSAGE((near(summary.mean_milliseconds, 30.0)),
                  "The overwritten sample must leave the mean.");
    CHECK_MESSAGE((near(summary.latest_milliseconds, 40.0)),
                  "Latest must survive ring wraparound.");
}

TEST_CASE("invalid samples and configuration fail safely") {
    ic2d::FrameTimeSeries series{2};
    CHECK_MESSAGE((!series.record_seconds(-0.1)), "Negative frame time must be rejected.");
    CHECK_MESSAGE((!series.record_seconds(std::numeric_limits<double>::infinity())),
                  "Infinite frame time must be rejected.");
    CHECK_MESSAGE((series.summary().sample_count == 0U),
                  "Rejected samples must not mutate telemetry state.");

    bool threw = false;
    try {
        const ic2d::FrameTimeSeries invalid{0};
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK_MESSAGE((threw), "A zero-capacity telemetry window must be rejected.");
}

TEST_CASE("clear keeps capacity and discards history") {
    ic2d::FrameTimeSeries series{4};
    static_cast<void>(series.record_seconds(0.016));
    series.clear();
    CHECK_MESSAGE((series.capacity() == 4U), "Clear must preserve preallocated storage.");
    CHECK_MESSAGE((series.summary().sample_count == 0U), "Clear must discard prior samples.");
}

} // namespace
