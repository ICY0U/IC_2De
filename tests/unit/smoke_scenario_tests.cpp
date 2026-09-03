#include <doctest/doctest.h>

#include "smoke_scenarios.hpp"

#include <set>
#include <string_view>

TEST_CASE("the registry is not empty and every entry is named and described") {
    const std::span<const ic2de_game::SmokeScenario> scenarios = ic2de_game::smoke_scenarios();
    CHECK(!scenarios.empty());

    for (const ic2de_game::SmokeScenario& scenario : scenarios) {
        CAPTURE(scenario.name);
        CHECK(!scenario.name.empty());
        // --help and --list-scenarios are generated from these, so an entry
        // without a description would silently print a blank line.
        CHECK(!scenario.description.empty());
        CHECK(scenario.apply != nullptr);
        // The name is appended to "--smoke-", so it must not repeat the prefix.
        CHECK(!scenario.name.starts_with("--"));
        CHECK(!scenario.name.starts_with("smoke-"));
    }
}

TEST_CASE("scenario names are unique") {
    // Two entries sharing a name would make one of them unreachable, and the
    // lookup would silently pick whichever came first.
    std::set<std::string_view> seen;
    for (const ic2de_game::SmokeScenario& scenario : ic2de_game::smoke_scenarios()) {
        CAPTURE(scenario.name);
        CHECK(seen.insert(scenario.name).second);
    }
}

TEST_CASE("lookup finds every registered scenario and rejects unknown ones") {
    for (const ic2de_game::SmokeScenario& scenario : ic2de_game::smoke_scenarios()) {
        CAPTURE(scenario.name);
        const ic2de_game::SmokeScenario* found = ic2de_game::find_smoke_scenario(scenario.name);
        REQUIRE(found != nullptr);
        CHECK(found->name == scenario.name);
    }

    CHECK(ic2de_game::find_smoke_scenario("") == nullptr);
    CHECK(ic2de_game::find_smoke_scenario("no-such-scenario") == nullptr);
    // The prefix is stripped before the lookup, so a name carrying it is wrong.
    CHECK(ic2de_game::find_smoke_scenario("--smoke-window") == nullptr);
}

TEST_CASE("every scenario ends on its own") {
    // A smoke run is started by CTest and nothing stops it from outside, so a
    // scenario that neither caps its ticks nor closes itself would hang the
    // suite rather than fail it. That is a hard failure to diagnose from a
    // timed-out build, and it is cheap to make impossible here.
    for (const ic2de_game::SmokeScenario& scenario : ic2de_game::smoke_scenarios()) {
        CAPTURE(scenario.name);
        ic2d::ApplicationConfig config;
        scenario.apply(config);

        const bool terminates = config.max_fixed_ticks > 0 || config.max_frames > 0 ||
                                config.close_after_editor_texture_hot_reload;
        CHECK(terminates);
    }
}

TEST_CASE("every scenario changes something") {
    // An entry that applies nothing is a scenario in name only: it would run
    // the default configuration and report success without testing anything.
    const ic2d::ApplicationConfig untouched;
    for (const ic2de_game::SmokeScenario& scenario : ic2de_game::smoke_scenarios()) {
        CAPTURE(scenario.name);
        ic2d::ApplicationConfig config;
        scenario.apply(config);

        const bool changed = config.max_fixed_ticks != untouched.max_fixed_ticks ||
                             config.max_frames != untouched.max_frames ||
                             config.capture_path != untouched.capture_path ||
                             config.start_with_editor != untouched.start_with_editor ||
                             config.automated_movement != untouched.automated_movement ||
                             config.automated_aim != untouched.automated_aim ||
                             config.close_after_editor_texture_hot_reload !=
                                 untouched.close_after_editor_texture_hot_reload;
        CHECK(changed);
    }
}
