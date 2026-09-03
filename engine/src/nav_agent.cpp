#include "ic2d/nav_agent.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] Vec2 direction_to(const Vec2 from, const Vec2 to) noexcept {
    const Vec2 delta{to.x - from.x, to.y - from.y};
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    constexpr float direction_epsilon = 0.0001F;
    if (!(length > direction_epsilon) || !std::isfinite(length)) {
        return {};
    }
    return {delta.x / length, delta.y / length};
}

[[nodiscard]] std::uint64_t next_repath_tick(
    const std::uint64_t tick,
    const std::uint32_t interval
) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    return tick > maximum - interval ? maximum : tick + interval;
}

} // namespace

struct NavAgentSystem::Impl {
    struct ActorState {
        EntityUuid actor{};
        EntityUuid target{};
        bool active{false};
        NavPathStatus path_status{NavPathStatus::unreachable};
        std::optional<NavCell> current_cell;
        std::optional<NavCell> goal_cell;
        std::vector<NavCell> path;
        std::size_t waypoint_index{0};
        // The last waypoint the agent actually arrived at, as distinct from the
        // one it is currently steering toward. Smoothing moves the target
        // ahead of the agent, so route loss has to be judged from where it has
        // really been or a smoothed agent looks like it has left its route.
        std::size_t reached_index{0};
        std::uint64_t next_repath_tick{1};
        std::uint64_t search_count{0};
        std::uint64_t waypoint_advance_count{0};
        Vec2 movement_direction{};
        float path_distance{0.0F};
        std::size_t expanded_cell_count{0};
    };

    NavAgentSettings settings{};
    std::map<std::uint64_t, ActorState> actors;
    std::uint64_t tick{0};
    std::uint64_t total_search_count{0};
};

NavAgentSystem::NavAgentSystem(const NavAgentSettings settings)
    : impl_{std::make_unique<Impl>()} {
    if (settings.repath_interval_ticks == 0 ||
        !std::isfinite(settings.waypoint_tolerance) ||
        !(settings.waypoint_tolerance > 0.0F)) {
        throw std::invalid_argument{
            "NavAgent settings require a positive repath interval and waypoint tolerance."};
    }
    impl_->settings = settings;
}

NavAgentSystem::~NavAgentSystem() = default;
NavAgentSystem::NavAgentSystem(NavAgentSystem&&) noexcept = default;
NavAgentSystem& NavAgentSystem::operator=(NavAgentSystem&&) noexcept = default;

bool NavAgentSystem::register_agent(const EntityUuid actor) noexcept {
    if (impl_->tick != 0 || !actor) {
        return false;
    }
    return impl_->actors.emplace(
        actor.value, Impl::ActorState{.actor = actor}).second;
}

std::vector<NavAgentMotion> NavAgentSystem::fixed_update(
    const std::uint64_t tick,
    const NavGrid& grid,
    const std::vector<NavAgentRequest>& requests
) {
    if (tick != impl_->tick + 1) {
        throw std::invalid_argument{
            "NavAgent fixed ticks must be one-based and sequential."};
    }
    if (requests.size() != impl_->actors.size()) {
        throw std::invalid_argument{
            "NavAgent requires one request per registered actor."};
    }

    std::unordered_map<std::uint64_t, const NavAgentRequest*> request_by_actor;
    request_by_actor.reserve(requests.size());
    for (const NavAgentRequest& request : requests) {
        if (!request.actor || !request.target || request.actor == request.target ||
            !finite(request.actor_position) || !finite(request.target_position) ||
            !request_by_actor.emplace(request.actor.value, &request).second) {
            throw std::invalid_argument{
                "NavAgent requests require unique identities and finite positions."};
        }
    }
    for (const auto& [actor_id, state] : impl_->actors) {
        static_cast<void>(state);
        if (!request_by_actor.contains(actor_id)) {
            throw std::invalid_argument{
                "NavAgent request identities must match registered agents."};
        }
    }

    std::vector<NavAgentMotion> motions;
    motions.reserve(impl_->actors.size());
    for (auto& [actor_id, state] : impl_->actors) {
        const NavAgentRequest& request = *request_by_actor.at(actor_id);
        const std::optional<NavCell> current_cell = grid.cell_at(request.actor_position);
        const std::optional<NavCell> goal_cell = grid.cell_at(request.target_position);
        const bool target_changed = state.target != request.target ||
                                    state.goal_cell != goal_cell;
        const bool activated = request.active && !state.active;
        state.target = request.target;
        state.current_cell = current_cell;
        state.goal_cell = goal_cell;
        state.movement_direction = {};
        bool repathed = false;

        if (!request.active) {
            state.active = false;
            state.path_status = NavPathStatus::unreachable;
            state.path.clear();
            state.waypoint_index = 0;
            state.reached_index = 0;
            state.path_distance = 0.0F;
            state.expanded_cell_count = 0;
            state.next_repath_tick = tick;
            motions.push_back({
                .actor = state.actor,
                .path_status = state.path_status,
            });
            continue;
        }
        state.active = true;

        bool route_lost = false;
        if (state.path_status == NavPathStatus::found && !state.path.empty() && current_cell) {
            const std::size_t first_remaining =
                state.reached_index == 0 ? 0 : state.reached_index - 1;
            route_lost = std::find(
                             state.path.begin() +
                                 static_cast<std::ptrdiff_t>(
                                     std::min(first_remaining, state.path.size())),
                             state.path.end(), *current_cell) == state.path.end();
        } else if (state.path_status == NavPathStatus::found) {
            route_lost = true;
        }

        const bool interval_due = tick >= state.next_repath_tick;
        const bool needs_path =
            activated || target_changed || route_lost || interval_due;
        if (needs_path) {
            const NavPathResult path = find_nav_path_world(
                grid, request.actor_position, request.target_position);
            state.path_status = path.status;
            state.path = path.cells;
            state.waypoint_index = path.status == NavPathStatus::found &&
                                           path.cells.size() > 1
                                       ? 1
                                       : path.cells.size();
            state.reached_index = state.waypoint_index;
            state.path_distance = path.total_distance;
            state.expanded_cell_count = path.expanded_cell_count;
            state.next_repath_tick = next_repath_tick(
                tick, impl_->settings.repath_interval_ticks);
            ++state.search_count;
            ++impl_->total_search_count;
            repathed = true;
        }

        if (state.path_status == NavPathStatus::found) {
            const float tolerance_squared = impl_->settings.waypoint_tolerance *
                                            impl_->settings.waypoint_tolerance;
            while (state.waypoint_index < state.path.size()) {
                const std::optional<NavGridCell> waypoint =
                    grid.cell(state.path[state.waypoint_index]);
                if (!waypoint) {
                    throw std::logic_error{
                        "NavAgent path referenced a cell absent from its source grid."};
                }
                const float delta_x = waypoint->center.x - request.actor_position.x;
                const float delta_z = waypoint->center.y - request.actor_position.y;
                if (delta_x * delta_x + delta_z * delta_z > tolerance_squared) {
                    break;
                }
                ++state.waypoint_index;
                ++state.waypoint_advance_count;
                state.reached_index = state.waypoint_index;
            }

            // Following cell centres one at a time makes an actor walk a
            // staircase: it has to reach the middle of every cell it passes
            // through before it may turn. Skipping ahead to the furthest
            // waypoint it can actually see turns that into a direct walk, and
            // rounds corners instead of squaring them.
            if (impl_->settings.smoothing_lookahead_cells > 0 &&
                state.waypoint_index < state.path.size()) {
                const std::size_t window = std::min<std::size_t>(
                    state.path.size(),
                    state.waypoint_index + impl_->settings.smoothing_lookahead_cells);
                for (std::size_t candidate = window; candidate > state.waypoint_index;
                     --candidate) {
                    const std::optional<NavGridCell> ahead =
                        grid.cell(state.path[candidate - 1]);
                    if (!ahead) {
                        throw std::logic_error{
                            "NavAgent path referenced a cell absent from its source grid."};
                    }
                    if (!nav_line_of_sight(grid, request.actor_position, ahead->center)) {
                        continue;
                    }
                    state.waypoint_advance_count +=
                        (candidate - 1) - state.waypoint_index;
                    state.waypoint_index = candidate - 1;
                    break;
                }
            }

            Vec2 destination = request.target_position;
            if (state.waypoint_index < state.path.size()) {
                // A visible target is walked at directly. The route exists to
                // get around what cannot be seen; once the target itself is in
                // sight, steering to a cell centre beside it only detours.
                if (!nav_line_of_sight(grid, request.actor_position,
                                       request.target_position)) {
                    const std::optional<NavGridCell> waypoint =
                        grid.cell(state.path[state.waypoint_index]);
                    if (!waypoint) {
                        throw std::logic_error{
                            "NavAgent path referenced a cell absent from its source grid."};
                    }
                    destination = waypoint->center;
                }
            }
            state.movement_direction = direction_to(request.actor_position, destination);
        }

        motions.push_back({
            .actor = state.actor,
            .movement_direction = state.movement_direction,
            .path_status = state.path_status,
            .repathed = repathed,
            .waypoint_index = state.waypoint_index,
            .path_cell_count = state.path.size(),
        });
    }
    impl_->tick = tick;
    return motions;
}

NavAgentSnapshot NavAgentSystem::snapshot() const {
    NavAgentSnapshot result{
        .tick = impl_->tick,
        .total_search_count = impl_->total_search_count,
        .repath_interval_ticks = impl_->settings.repath_interval_ticks,
        .waypoint_tolerance = impl_->settings.waypoint_tolerance,
    };
    result.actors.reserve(impl_->actors.size());
    for (const auto& [actor_id, state] : impl_->actors) {
        static_cast<void>(actor_id);
        result.actors.push_back({
            .actor = state.actor,
            .target = state.target,
            .active = state.active,
            .path_status = state.path_status,
            .current_cell = state.current_cell,
            .goal_cell = state.goal_cell,
            .path = state.path,
            .waypoint_index = state.waypoint_index,
            .next_repath_tick = state.next_repath_tick,
            .search_count = state.search_count,
            .waypoint_advance_count = state.waypoint_advance_count,
            .movement_direction = state.movement_direction,
            .path_distance = state.path_distance,
            .expanded_cell_count = state.expanded_cell_count,
        });
    }
    return result;
}

void NavAgentSystem::reset() noexcept {
    for (auto& [actor_id, state] : impl_->actors) {
        static_cast<void>(actor_id);
        const EntityUuid actor = state.actor;
        state = Impl::ActorState{.actor = actor};
    }
    impl_->tick = 0;
    impl_->total_search_count = 0;
}

} // namespace ic2d
