#pragma once

#include <cstddef>
#include <vector>

namespace ic2d {

struct FrameTimingSummary {
    std::size_t sample_count{0};
    double latest_milliseconds{0.0};
    double mean_milliseconds{0.0};
    double p50_milliseconds{0.0};
    double p95_milliseconds{0.0};
    double p99_milliseconds{0.0};
};

// A fixed-capacity rolling window. Storage is allocated once so recording a
// frame never allocates or invalidates callers through an exposed container.
class FrameTimeSeries final {
public:
    explicit FrameTimeSeries(std::size_t capacity);

    // Invalid or negative samples are rejected without disturbing the window.
    [[nodiscard]] bool record_seconds(double seconds) noexcept;
    [[nodiscard]] FrameTimingSummary summary() const;
    [[nodiscard]] std::size_t capacity() const noexcept;
    void clear() noexcept;

private:
    std::vector<double> samples_;
    mutable std::vector<double> scratch_;
    std::size_t next_index_{0};
    std::size_t sample_count_{0};
};

} // namespace ic2d
