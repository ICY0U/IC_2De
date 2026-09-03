#pragma once

#include <cstdint>

namespace ic2d {

// Whether the scene is being edited, played, or held.
//
// Editing is not a pause. A paused run is a run that has already happened and
// is waiting to continue; editing is the authored scene, untouched, exactly as
// the document describes it. An editor that opened onto a paused run would be
// showing a state no document records, which is why the two are named apart
// rather than sharing one flag.
//
// This lives outside ic2d/editor.hpp because it is not an editor concept. The
// application loop reads it to decide whether the fixed clock advances at all,
// and pause and single-step are runtime controls that the shipping build has
// as much as the development one. While it was declared in the editor header
// the shipping configuration did not compile, because the loop referred to a
// type that the development-tools-off build never saw.
enum class RunState : std::uint8_t {
    editing,
    running,
    paused,
};

// True when fixed ticks are advancing.
[[nodiscard]] constexpr bool simulating(const RunState state) noexcept {
    return state == RunState::running;
}

} // namespace ic2d
