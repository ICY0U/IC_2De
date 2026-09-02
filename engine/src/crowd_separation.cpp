#include "ic2d/crowd_separation.hpp"

#include "ic2d/jobs.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ic2d {
namespace {

[[nodiscard]] std::int32_t cell_index(const float value, const float cell_size) noexcept {
    return static_cast<std::int32_t>(std::floor(value / cell_size));
}

[[nodiscard]] std::uint32_t cell_hash(
    const std::int32_t column,
    const std::int32_t row
) noexcept {
    const auto x = static_cast<std::uint32_t>(column) * 0x9E3779B9U;
    const auto y = static_cast<std::uint32_t>(row) * 0x85EBCA6BU;
    std::uint32_t mixed = x ^ (y + 0x165667B1U + (x << 6U) + (x >> 2U));
    mixed ^= mixed >> 15U;
    mixed *= 0x2C1B3C6DU;
    mixed ^= mixed >> 12U;
    return mixed;
}

[[nodiscard]] Vec2 normalized(const Vec2 value) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    if (!(length > 0.0F)) {
        return {};
    }
    return {value.x / length, value.y / length};
}

} // namespace

std::vector<CrowdSteer> resolve_crowd_separation(
    const std::vector<CrowdAgent>& agents,
    const CrowdSeparationSettings& settings,
    JobSystem* const jobs
) {
    std::vector<CrowdSteer> steers;
    steers.reserve(agents.size());
    for (const CrowdAgent& agent : agents) {
        steers.push_back({
            .actor = agent.actor,
            .direction = normalized(agent.desired_direction),
            .separated = false,
        });
    }
    if (agents.size() < 2 || !(settings.radius > 0.0F)) {
        return steers;
    }

    const float radius = settings.radius;
    const float radius_squared = radius * radius;
    const std::size_t agent_count = agents.size();

    // A hash map of per-cell vectors allocates once per occupied cell every
    // tick, which at thousands of actors costs more than the steering itself.
    // These are flat arrays filled by counting sort instead: four allocations
    // for the whole crowd, and neighbours of a cell end up contiguous.
    std::size_t bucket_count = 1;
    while (bucket_count < agent_count * 2U) {
        bucket_count <<= 1U;
    }
    const std::uint32_t bucket_mask = static_cast<std::uint32_t>(bucket_count - 1U);

    std::vector<std::int32_t> columns(agent_count);
    std::vector<std::int32_t> rows(agent_count);
    std::vector<std::uint32_t> bucket_of(agent_count);
    std::vector<std::uint32_t> bucket_start(bucket_count + 1U, 0U);
    for (std::size_t index = 0; index < agent_count; ++index) {
        columns[index] = cell_index(agents[index].position.x, radius);
        rows[index] = cell_index(agents[index].position.y, radius);
        bucket_of[index] = cell_hash(columns[index], rows[index]) & bucket_mask;
        ++bucket_start[bucket_of[index] + 1U];
    }
    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        bucket_start[bucket + 1U] += bucket_start[bucket];
    }
    std::vector<std::uint32_t> cursor(bucket_start.begin(), bucket_start.end() - 1);
    std::vector<std::uint32_t> bucket_items(agent_count);
    for (std::size_t index = 0; index < agent_count; ++index) {
        bucket_items[cursor[bucket_of[index]]++] = static_cast<std::uint32_t>(index);
    }

    const auto steer_range = [&](const std::size_t first, const std::size_t last) {
    for (std::size_t index = first; index < last; ++index) {
        const CrowdAgent& agent = agents[index];
        Vec2 push{};
        for (std::int32_t row_offset = -1; row_offset <= 1; ++row_offset) {
            for (std::int32_t column_offset = -1; column_offset <= 1; ++column_offset) {
                const std::int32_t column = columns[index] + column_offset;
                const std::int32_t row = rows[index] + row_offset;
                const std::uint32_t bucket = cell_hash(column, row) & bucket_mask;
                for (std::uint32_t slot = bucket_start[bucket];
                     slot < bucket_start[bucket + 1U]; ++slot) {
                    const std::uint32_t other_index = bucket_items[slot];
                    // Buckets are shared by colliding cells, so confirm the
                    // neighbour really is in the cell being visited.
                    if (columns[other_index] != column || rows[other_index] != row) {
                        continue;
                    }
                    if (other_index == index) {
                        continue;
                    }
                    const CrowdAgent& other = agents[other_index];
                    const float delta_x = agent.position.x - other.position.x;
                    const float delta_y = agent.position.y - other.position.y;
                    const float distance_squared = delta_x * delta_x + delta_y * delta_y;
                    if (distance_squared >= radius_squared) {
                        continue;
                    }
                    if (distance_squared <= 0.0F) {
                        // Exactly coincident actors have no gradient to follow.
                        // Fan them apart by input order so the result is stable
                        // instead of dependent on floating-point noise.
                        const float angle =
                            static_cast<float>(index % 8U) * 0.78539816F;
                        push.x += std::cos(angle);
                        push.y += std::sin(angle);
                        continue;
                    }
                    const float distance = std::sqrt(distance_squared);
                    // Linear falloff: touching actors push hardest, actors at
                    // the radius contribute nothing and cannot cause a jump.
                    const float weight = (radius - distance) / radius;
                    push.x += delta_x / distance * weight;
                    push.y += delta_y / distance * weight;
                }
            }
        }

        if (push.x == 0.0F && push.y == 0.0F) {
            continue;
        }
        const Vec2 desired = steers[index].direction;
        const Vec2 blended{
            desired.x + push.x * settings.strength,
            desired.y + push.y * settings.strength,
        };
        steers[index].direction = normalized(blended);
        steers[index].separated = true;
    }
    };

    // Below this the thread hand-off costs more than the work it hands off.
    constexpr std::size_t parallel_agent_threshold = 512;
    constexpr std::size_t minimum_batch = 256;
    if (jobs != nullptr && agent_count >= parallel_agent_threshold) {
        jobs->parallel_for(agent_count, minimum_batch, steer_range);
    } else {
        steer_range(0, agent_count);
    }
    return steers;
}

} // namespace ic2d
