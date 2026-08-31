#include "ic2d/jobs.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace ic2d {

struct JobSystem::Impl {
    std::mutex mutex;
    std::condition_variable work_available;
    std::condition_variable idle;
    std::deque<Job> queue;
    std::vector<std::thread> workers;
    std::size_t active_workers{0};
    bool stopping{false};
    std::exception_ptr first_failure;
};

std::size_t JobSystem::recommended_worker_count() noexcept {
    constexpr std::size_t maximum_workers = 16;
    const auto hardware_threads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    const std::size_t available = hardware_threads > 1 ? hardware_threads - 1 : 1;
    return std::min(available, maximum_workers);
}

JobSystem::JobSystem(std::size_t worker_count)
    : impl_{std::make_unique<Impl>()} {
    if (worker_count == 0) {
        worker_count = recommended_worker_count();
    }

    impl_->workers.reserve(worker_count);
    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        impl_->workers.emplace_back([this] {
            while (true) {
                Job job;
                {
                    std::unique_lock lock{impl_->mutex};
                    impl_->work_available.wait(lock, [this] {
                        return impl_->stopping || !impl_->queue.empty();
                    });
                    if (impl_->stopping && impl_->queue.empty()) {
                        return;
                    }
                    job = std::move(impl_->queue.front());
                    impl_->queue.pop_front();
                    ++impl_->active_workers;
                }

                try {
                    job();
                } catch (...) {
                    std::scoped_lock lock{impl_->mutex};
                    if (!impl_->first_failure) {
                        impl_->first_failure = std::current_exception();
                    }
                }

                {
                    std::scoped_lock lock{impl_->mutex};
                    --impl_->active_workers;
                    if (impl_->queue.empty() && impl_->active_workers == 0) {
                        impl_->idle.notify_all();
                    }
                }
            }
        });
    }
}

JobSystem::~JobSystem() {
    {
        std::scoped_lock lock{impl_->mutex};
        impl_->stopping = true;
    }
    impl_->work_available.notify_all();
    for (std::thread& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void JobSystem::submit(Job job) {
    if (!job) {
        throw std::invalid_argument{"Cannot submit an empty job."};
    }

    {
        std::scoped_lock lock{impl_->mutex};
        if (impl_->stopping) {
            throw std::runtime_error{"Cannot submit work to a stopping job system."};
        }
        impl_->queue.push_back(std::move(job));
    }
    impl_->work_available.notify_one();
}

void JobSystem::wait_idle() {
    std::exception_ptr failure;
    {
        std::unique_lock lock{impl_->mutex};
        impl_->idle.wait(lock, [this] {
            return impl_->queue.empty() && impl_->active_workers == 0;
        });
        failure = impl_->first_failure;
        impl_->first_failure = nullptr;
    }

    if (failure) {
        std::rethrow_exception(failure);
    }
}

void JobSystem::parallel_for(
    const std::size_t item_count,
    const std::size_t minimum_batch_size,
    const RangeJob& job
) {
    if (item_count == 0) {
        return;
    }
    if (!job) {
        throw std::invalid_argument{"Cannot execute an empty range job."};
    }

    const std::size_t safe_minimum = std::max<std::size_t>(minimum_batch_size, 1);
    const std::size_t maximum_job_count = (item_count + safe_minimum - 1) / safe_minimum;
    const std::size_t job_count = std::max<std::size_t>(1, std::min(worker_count(), maximum_job_count));
    const std::size_t batch_size = (item_count + job_count - 1) / job_count;

    for (std::size_t begin = 0; begin < item_count; begin += batch_size) {
        const std::size_t end = std::min(begin + batch_size, item_count);
        submit([job, begin, end] {
            job(begin, end);
        });
    }
    wait_idle();
}

std::size_t JobSystem::worker_count() const noexcept {
    return impl_->workers.size();
}

} // namespace ic2d
