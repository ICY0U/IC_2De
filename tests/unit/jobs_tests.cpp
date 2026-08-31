#include "ic2d/jobs.hpp"

#include <atomic>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
void test_submitted_jobs_complete() {
    ic2d::JobSystem jobs{4};
    std::atomic<int> completed{0};
    for (int index = 0; index < 256; ++index) {
        jobs.submit([&completed] {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    jobs.wait_idle();
    expect(completed.load(std::memory_order_relaxed) == 256, "All submitted jobs must complete.");
    expect(jobs.worker_count() == 4, "Explicit worker count must be respected.");
}

void test_parallel_for_covers_each_item_once() {
    ic2d::JobSystem jobs{4};
    std::vector<int> values(10'000, 0);
    jobs.parallel_for(values.size(), 128, [&values](const std::size_t begin, const std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            values[index] = static_cast<int>(index + 1);
        }
    });

    bool correct = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
        correct = correct && values[index] == static_cast<int>(index + 1);
    }
    expect(correct, "Parallel range jobs must cover every item exactly once.");
}

void test_worker_failure_reaches_caller() {
    ic2d::JobSystem jobs{2};
    jobs.submit([] {
        throw std::runtime_error{"expected test failure"};
    });

    bool rethrown = false;
    try {
        jobs.wait_idle();
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    expect(rethrown, "Worker exceptions must be rethrown by wait_idle.");
}

} // namespace

int main() {
    test_submitted_jobs_complete();
    test_parallel_for_covers_each_item_once();
    test_worker_failure_reaches_caller();

    if (failures == 0) {
        std::cout << "All job-system tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
