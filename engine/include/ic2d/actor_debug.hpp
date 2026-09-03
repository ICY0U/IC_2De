#pragma once

#include "ic2d/identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace ic2d {

// A development override an author holds over one actor while the scene runs.
//
// These are deliberately states rather than one-shot commands: an author turns
// a Runner off, walks the player past it, and turns it back on, and the answer
// to "is this actor frozen" has to survive every tick in between. Killing an
// actor is the opposite, a single event with a lasting consequence the health
// module already owns, so it is not one of these.
enum class ActorDebugFlag : std::uint8_t {
    // Held where it stands: no pursuit, no attacks, no shuffling out of a
    // crowd. The actor is still solid, still shootable, still alive.
    frozen,
    // Damage aimed at the actor is discarded before it reaches health, so the
    // run continues rather than the actor simply absorbing it.
    invulnerable,
    // Firing costs no rounds and the magazine never needs reloading.
    infinite_ammo,
    count,
};

inline constexpr std::size_t actor_debug_flag_count =
    static_cast<std::size_t>(ActorDebugFlag::count);

[[nodiscard]] std::string_view actor_debug_flag_name(ActorDebugFlag flag) noexcept;

struct ActorDebugStateSnapshot {
    EntityUuid actor{};
    std::array<bool, actor_debug_flag_count> flags{};

    [[nodiscard]] bool enabled(ActorDebugFlag flag) const noexcept {
        return flag != ActorDebugFlag::count &&
               flags[static_cast<std::size_t>(flag)];
    }
};

struct ActorDebugSnapshot {
    std::size_t overridden_actor_count{0};
    // Canonical actor UUID order, independent of the order flags were set, so
    // a panel listing them does not reshuffle as an author clicks.
    std::vector<ActorDebugStateSnapshot> actors;

    [[nodiscard]] bool enabled(EntityUuid actor, ActorDebugFlag flag) const noexcept;
};

// Owns which development overrides are held over which actors, and nothing
// else. It applies no override itself: gameplay modules stay unaware that the
// editor exists, and the application asks this before it feeds them.
//
// Only actors carrying at least one override are stored, so a scene with ten
// thousand Runners and one frozen one costs one entry.
class ActorDebugOverrides final {
public:
    ActorDebugOverrides();
    ~ActorDebugOverrides();

    ActorDebugOverrides(const ActorDebugOverrides&) = delete;
    ActorDebugOverrides& operator=(const ActorDebugOverrides&) = delete;
    ActorDebugOverrides(ActorDebugOverrides&&) noexcept;
    ActorDebugOverrides& operator=(ActorDebugOverrides&&) noexcept;

    // False without mutating anything when the identity or flag is invalid.
    [[nodiscard]] bool set(EntityUuid actor, ActorDebugFlag flag, bool enabled) noexcept;
    [[nodiscard]] bool enabled(EntityUuid actor, ActorDebugFlag flag) const noexcept;
    [[nodiscard]] bool any(EntityUuid actor) const noexcept;

    // True when an override was actually dropped, so a caller can report it.
    bool clear(EntityUuid actor) noexcept;
    void clear_all() noexcept;

    [[nodiscard]] ActorDebugSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
