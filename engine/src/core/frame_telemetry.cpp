#include "ic2d/core/frame_telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <stdexcept>

namespace ic2d {
namespace {

[[nodiscard]] double percentile(const std::span<const double> sorted_samples,
                                const double fraction) noexcept {
    if (sorted_samples.empty()) {
        return 0.0;
    }
    const double rank = std::ceil(fraction * static_cast<double>(sorted_samples.size()));
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1U;
    return sorted_samples[std::min(index, sorted_samples.size() - 1U)];
}

} // namespace

FrameTimeSeries::FrameTimeSeries(const std::size_t capacity)
    : samples_(capacity, 0.0), scratch_(capacity, 0.0) {
    if (capacity == 0U) {
        throw std::invalid_argument{"Frame telemetry capacity must be greater than zero."};
    }
}

bool FrameTimeSeries::record_seconds(const double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return false;
    }
    samples_[next_index_] = seconds * 1'000.0;
    next_index_ = (next_index_ + 1U) % samples_.size();
    sample_count_ = std::min(sample_count_ + 1U, samples_.size());
    return true;
}

FrameTimingSummary FrameTimeSeries::summary() const {
    if (sample_count_ == 0U) {
        return {};
    }

    std::copy_n(samples_.begin(), sample_count_, scratch_.begin());
    const std::span<double> sorted_samples{scratch_.data(), sample_count_};
    std::ranges::sort(sorted_samples);
    const std::size_t latest_index = (next_index_ + samples_.size() - 1U) % samples_.size();
    const double total = std::accumulate(sorted_samples.begin(), sorted_samples.end(), 0.0);
    return {
        .sample_count = sample_count_,
        .latest_milliseconds = samples_[latest_index],
        .mean_milliseconds = total / static_cast<double>(sample_count_),
        .p50_milliseconds = percentile(sorted_samples, 0.50),
        .p95_milliseconds = percentile(sorted_samples, 0.95),
        .p99_milliseconds = percentile(sorted_samples, 0.99),
    };
}

std::size_t FrameTimeSeries::capacity() const noexcept { return samples_.size(); }

void FrameTimeSeries::clear() noexcept {
    next_index_ = 0U;
    sample_count_ = 0U;
}

} // namespace ic2d
