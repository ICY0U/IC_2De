#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace ic2d {

class JobSystem final {
public:
    using Job = std::function<void()>;
    using RangeJob = std::function<void(std::size_t begin, std::size_t end)>;

    explicit JobSystem(std::size_t worker_count = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    void submit(Job job);
    void wait_idle();
    void parallel_for(std::size_t item_count, std::size_t minimum_batch_size, const RangeJob& job);

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] static std::size_t recommended_worker_count() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
