#include "ic2d/actor_debug.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace ic2d {

std::string_view actor_debug_flag_name(const ActorDebugFlag flag) noexcept {
    switch (flag) {
    case ActorDebugFlag::frozen:
        return "frozen";
    case ActorDebugFlag::invulnerable:
        return "invulnerable";
    case ActorDebugFlag::infinite_ammo:
        return "infinite ammo";
    case ActorDebugFlag::count:
        break;
    }
    return "unknown";
}

bool ActorDebugSnapshot::enabled(const EntityUuid actor, const ActorDebugFlag flag) const noexcept {
    const auto found = std::ranges::find(actors, actor, &ActorDebugStateSnapshot::actor);
    return found != actors.end() && found->enabled(flag);
}

struct ActorDebugOverrides::Impl {
    // Ordered by UUID, which is what makes the snapshot canonical without a
    // sort at every read.
    std::map<std::uint64_t, std::array<bool, actor_debug_flag_count>> actors;
};

ActorDebugOverrides::ActorDebugOverrides() : impl_{std::make_unique<Impl>()} {}
ActorDebugOverrides::~ActorDebugOverrides() = default;
ActorDebugOverrides::ActorDebugOverrides(ActorDebugOverrides&&) noexcept = default;
ActorDebugOverrides& ActorDebugOverrides::operator=(ActorDebugOverrides&&) noexcept = default;

bool ActorDebugOverrides::set(const EntityUuid actor, const ActorDebugFlag flag,
                              const bool enabled) noexcept {
    if (!actor || flag == ActorDebugFlag::count) {
        return false;
    }
    const auto index = static_cast<std::size_t>(flag);
    const auto found = impl_->actors.find(actor.value);
    if (found == impl_->actors.end()) {
        if (!enabled) {
            // Clearing a flag nobody holds is a success that stores nothing.
            return true;
        }
        std::array<bool, actor_debug_flag_count> flags{};
        flags[index] = true;
        impl_->actors.emplace(actor.value, flags);
        return true;
    }
    found->second[index] = enabled;
    // An actor back to its authored behaviour is not an override, and keeping
    // the entry would leave the snapshot claiming it is one.
    if (std::ranges::none_of(found->second, [](const bool held) { return held; })) {
        impl_->actors.erase(found);
    }
    return true;
}

bool ActorDebugOverrides::enabled(const EntityUuid actor,
                                  const ActorDebugFlag flag) const noexcept {
    if (!actor || flag == ActorDebugFlag::count) {
        return false;
    }
    const auto found = impl_->actors.find(actor.value);
    return found != impl_->actors.end() && found->second[static_cast<std::size_t>(flag)];
}

bool ActorDebugOverrides::any(const EntityUuid actor) const noexcept {
    return actor && impl_->actors.contains(actor.value);
}

bool ActorDebugOverrides::clear(const EntityUuid actor) noexcept {
    return actor && impl_->actors.erase(actor.value) > 0;
}

void ActorDebugOverrides::clear_all() noexcept { impl_->actors.clear(); }

ActorDebugSnapshot ActorDebugOverrides::snapshot() const {
    ActorDebugSnapshot result{.overridden_actor_count = impl_->actors.size()};
    result.actors.reserve(impl_->actors.size());
    for (const auto& [actor, flags] : impl_->actors) {
        result.actors.push_back({.actor = EntityUuid{actor}, .flags = flags});
    }
    return result;
}

} // namespace ic2d
