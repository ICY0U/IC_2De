#pragma once

#include <compare>
#include <cstdint>

namespace ic2d {

// A transient handle valid only for the lifetime of one World instance.
struct EntityId {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    auto operator<=>(const EntityId&) const = default;
};

// Stable authored identity retained across World snapshots and runtime copies.
struct EntityUuid {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    auto operator<=>(const EntityUuid&) const = default;
};

} // namespace ic2d
