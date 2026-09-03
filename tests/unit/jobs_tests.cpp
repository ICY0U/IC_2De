#include <doctest/doctest.h>

#include "ic2d/jobs.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

TEST_CASE("submitted jobs complete") {
    ic2d::JobSystem jobs{4};
    std::atomic<int> completed{0};
    for (int index = 0; index < 256; ++index) {
        jobs.submit([&completed] { completed.fetch_add(1, std::memory_order_relaxed); });
    }
    jobs.wait_idle();
    CHECK_MESSAGE((completed.load(std::memory_order_relaxed) == 256),
                  "All submitted jobs must complete.");
    CHECK_MESSAGE((jobs.worker_count() == 4), "Explicit worker count must be respected.");
}

TEST_CASE("parallel for covers each item once") {
    ic2d::JobSystem jobs{4};
    std::vector<int> values(10'000, 0);
    jobs.parallel_for(values.size(), 128,
                      [&values](const std::size_t begin, const std::size_t end) {
                          for (std::size_t index = begin; index < end; ++index) {
                              values[index] = static_cast<int>(index + 1);
                          }
                      });

    bool correct = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
        correct = correct && values[index] == static_cast<int>(index + 1);
    }
    CHECK_MESSAGE((correct), "Parallel range jobs must cover every item exactly once.");
}

TEST_CASE("worker failure reaches caller") {
    ic2d::JobSystem jobs{2};
    jobs.submit([] { throw std::runtime_error{"expected test failure"}; });

    bool rethrown = false;
    try {
        jobs.wait_idle();
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    CHECK_MESSAGE((rethrown), "Worker exceptions must be rethrown by wait_idle.");
}

} // namespace
