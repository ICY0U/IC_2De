#pragma once

#include "ic2d/application.hpp"

#include <span>
#include <string_view>

namespace ic2de_game {

// One deterministic smoke run: what it is called, what it proves, and the
// configuration that produces it.
//
// These live in the game rather than the engine on purpose. A value such as the
// aim direction {0.98359346F, 0.18039931F} is the camera-space bearing from the
// player to a particular actor in test_area.scene: it describes this game's
// authored content, not any capability of the engine. Keeping the table here is
// what allows ApplicationConfig to stay a description of an application rather
// than a list of this project's test thresholds.
struct SmokeScenario {
    // The name as it appears after "--smoke-".
    std::string_view name;
    // One line, shown by --help and --list-scenarios.
    std::string_view description;
    // Applies the scenario to an otherwise default configuration.
    void (*apply)(ic2d::ApplicationConfig& config);
};

// Every registered scenario, in the order they are listed to a reader.
[[nodiscard]] std::span<const SmokeScenario> smoke_scenarios() noexcept;

// The scenario with this name, or nullptr. The name excludes the "--smoke-"
// prefix.
[[nodiscard]] const SmokeScenario* find_smoke_scenario(std::string_view name) noexcept;

} // namespace ic2de_game
