cmake_minimum_required(VERSION 3.24)

project(
    IC2DE
    VERSION 0.1.0
    DESCRIPTION "A focused production-quality 2D-first 2.5D game engine"
    LANGUAGES C CXX
)

option(IC2DE_BUILD_TESTS "Build IC2DE automated tests" ON)
option(IC2DE_BUILD_TESTBED "Build the development testbed executable" ON)
option(IC2DE_BUILD_SHIPPING_RUNTIME "Build the shipping runtime executable" ON)
option(IC2DE_ENABLE_DEVELOPMENT_TOOLS "Compile development overlays and controls" ON)
option(IC2DE_WARNINGS_AS_ERRORS "Treat warnings in IC2DE code as errors" ON)
option(IC2DE_ENABLE_CLANG_TIDY "Run clang-tidy over IC2DE code during the build" OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(raylib entt box2d nlohmann_json)

# ---------------------------------------------------------------------------
# Project-wide target settings
# ---------------------------------------------------------------------------

function(ic2de_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor)
        if(IC2DE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(IC2DE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(IC2DE_ENABLE_CLANG_TIDY)
    find_program(IC2DE_CLANG_TIDY_COMMAND NAMES clang-tidy)
    if(NOT IC2DE_CLANG_TIDY_COMMAND)
        message(FATAL_ERROR
            "IC2DE_ENABLE_CLANG_TIDY is ON but clang-tidy was not found on PATH."
        )
    endif()
endif()

# Analysis is applied per target rather than through CMAKE_CXX_CLANG_TIDY, which
# would also run over the fetched raylib, Box2D, EnTT and Dear ImGui sources.
# Those are not ours to fix, and their findings would bury the project's own.
function(ic2de_set_project_analysis target)
    if(IC2DE_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES
            CXX_CLANG_TIDY "${IC2DE_CLANG_TIDY_COMMAND}"
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Declaration helpers
#
# Every engine module is a static library with the same shape: engine/include on
# its public interface, C++20, project warnings and project analysis. Spelling
# that out per target cost roughly five hundred lines and made it possible to
# add a target that quietly opts out of the warning settings. These helpers make
# the shared parts structural, so a module differs from its neighbours only
# where it genuinely differs.
# ---------------------------------------------------------------------------

# ic2de_add_module(<name>
#     ALIAS <AliasName>                 # exported as IC2DE::<AliasName>
#     SOURCES <file>...
#     [PUBLIC_DEPS <target>...]
#     [PRIVATE_DEPS <target>...]
#     [PRIVATE_INCLUDES <dir>...]
# )
#
# Declares the static library ic2de_<name>.
function(ic2de_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg
        ""
        "ALIAS"
        "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS;PRIVATE_INCLUDES"
    )

    # A mistyped keyword would otherwise be silently swallowed and the setting
    # it was meant to carry silently lost.
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "ic2de_add_module(${name}): unrecognised arguments: ${arg_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT arg_ALIAS)
        message(FATAL_ERROR "ic2de_add_module(${name}): ALIAS is required.")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "ic2de_add_module(${name}): SOURCES is required.")
    endif()

    set(target "ic2de_${name}")

    add_library(${target} STATIC ${arg_SOURCES})
    add_library(IC2DE::${arg_ALIAS} ALIAS ${target})

    target_include_directories(${target} PUBLIC engine/include)
    if(arg_PRIVATE_INCLUDES)
        target_include_directories(${target} PRIVATE ${arg_PRIVATE_INCLUDES})
    endif()
    if(arg_PUBLIC_DEPS)
        target_link_libraries(${target} PUBLIC ${arg_PUBLIC_DEPS})
    endif()
    if(arg_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_DEPS})
    endif()

    target_compile_features(${target} PUBLIC cxx_std_20)
    ic2de_set_project_warnings(${target})
    ic2de_set_project_analysis(${target})
endfunction()

# ic2de_add_executable(<name>
#     SOURCES <file>...
#     [DEPS <target>...]
#     [DEFINITIONS <define>...]
# )
#
# Declares the executable ic2de_<name>. Output names and other per-executable
# properties stay at the call site, because they are what distinguishes one
# executable from another.
function(ic2de_add_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPS;DEFINITIONS")

    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "ic2de_add_executable(${name}): unrecognised arguments: ${arg_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "ic2de_add_executable(${name}): SOURCES is required.")
    endif()

    set(target "ic2de_${name}")

    add_executable(${target} ${arg_SOURCES})
    if(arg_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_DEPS})
    endif()
    if(arg_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS})
    endif()

    target_compile_features(${target} PRIVATE cxx_std_20)
    ic2de_set_project_warnings(${target})
    ic2de_set_project_analysis(${target})
endfunction()

# ic2de_add_test(<name>
#     SOURCES <file>...
#     DEPS <target>...
#     [PRIVATE_INCLUDES <dir>...]
#     [DEFINITIONS <define>...]
#     [TEST_NAME <ctest-name>]          # defaults to ic2de.<name>
# )
#
# Declares the executable ic2de_<name>_tests and registers it with CTest.
function(ic2de_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg
        ""
        "TEST_NAME"
        "SOURCES;DEPS;PRIVATE_INCLUDES;DEFINITIONS"
    )

    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "ic2de_add_test(${name}): unrecognised arguments: ${arg_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "ic2de_add_test(${name}): SOURCES is required.")
    endif()

    set(target "ic2de_${name}_tests")
    if(arg_TEST_NAME)
        set(test_name "${arg_TEST_NAME}")
    else()
        set(test_name "ic2de.${name}")
    endif()

    add_executable(${target} ${arg_SOURCES})
    if(arg_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_DEPS})
    endif()
    if(arg_PRIVATE_INCLUDES)
        target_include_directories(${target} PRIVATE ${arg_PRIVATE_INCLUDES})
    endif()
    if(arg_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS})
    endif()

    target_compile_features(${target} PRIVATE cxx_std_20)
    ic2de_set_project_warnings(${target})
    ic2de_set_project_analysis(${target})

    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

# ic2de_add_smoke_test(<ctest-name>
#     TARGET <executable-target>
#     [ARGS <arg>...]
# )
#
# Registers a test that drives a real executable. These open a window and need a
# GPU, so they carry the "gpu" label: a headless machine, continuous integration
# included, runs the rest of the suite with `ctest -LE gpu`.
function(ic2de_add_smoke_test test_name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TARGET" "ARGS")

    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "ic2de_add_smoke_test(${test_name}): unrecognised arguments: ${arg_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT arg_TARGET)
        message(FATAL_ERROR "ic2de_add_smoke_test(${test_name}): TARGET is required.")
    endif()

    add_test(NAME ${test_name} COMMAND ${arg_TARGET} ${arg_ARGS})
    set_tests_properties(${test_name} PROPERTIES
        LABELS gpu
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${arg_TARGET}>"
    )
endfunction()

# ---------------------------------------------------------------------------
# Engine modules
# ---------------------------------------------------------------------------

ic2de_add_module(core ALIAS Core
    SOURCES
        engine/src/core/automated_run.cpp
        engine/src/core/fixed_step_clock.cpp
        engine/src/core/frame_telemetry.cpp
        engine/src/core/log.cpp
)

ic2de_add_module(flow ALIAS Flow SOURCES engine/src/layer_stack.cpp)

ic2de_add_module(jobs ALIAS Jobs SOURCES engine/src/jobs.cpp)

ic2de_add_module(project ALIAS Project SOURCES engine/src/runtime_project.cpp)

ic2de_add_module(ground ALIAS Ground SOURCES engine/src/ground_map.cpp)

ic2de_add_module(nav_grid ALIAS NavGrid
    SOURCES engine/src/nav_grid.cpp
    PUBLIC_DEPS IC2DE::Ground
)

ic2de_add_module(nav_pathfinding ALIAS NavPathfinding
    SOURCES engine/src/nav_pathfinding.cpp
    PUBLIC_DEPS IC2DE::NavGrid
)

ic2de_add_module(nav_agent ALIAS NavAgent
    SOURCES engine/src/nav_agent.cpp
    PUBLIC_DEPS IC2DE::NavPathfinding
)

ic2de_add_module(flow_field ALIAS FlowField
    SOURCES engine/src/flow_field.cpp
    PUBLIC_DEPS IC2DE::NavGrid
)

ic2de_add_module(crowd ALIAS Crowd
    SOURCES engine/src/crowd_separation.cpp
    PUBLIC_DEPS IC2DE::Jobs
)

ic2de_add_module(physics ALIAS Physics
    SOURCES engine/src/physics2d.cpp
    PRIVATE_DEPS box2d::box2d
)

ic2de_add_module(input ALIAS Input SOURCES engine/src/input.cpp)

ic2de_add_module(combat ALIAS Combat SOURCES engine/src/combat.cpp)

ic2de_add_module(enemy_intent ALIAS EnemyIntent SOURCES engine/src/enemy_intent.cpp)

ic2de_add_module(projectiles ALIAS Projectiles
    SOURCES engine/src/projectiles.cpp
    PUBLIC_DEPS IC2DE::Combat
)

ic2de_add_module(actor_debug ALIAS ActorDebug SOURCES engine/src/actor_debug.cpp)

ic2de_add_module(health ALIAS Health SOURCES engine/src/health.cpp)

ic2de_add_module(gameplay_state ALIAS GameplayState
    SOURCES engine/src/gameplay_state.cpp
    PUBLIC_DEPS
        IC2DE::Combat
        IC2DE::EnemyIntent
        IC2DE::Health
        IC2DE::NavAgent
        IC2DE::Projectiles
        IC2DE::World
)

ic2de_add_module(presentation ALIAS Presentation SOURCES engine/src/presentation.cpp)

ic2de_add_module(locomotion ALIAS Locomotion SOURCES engine/src/locomotion.cpp)

ic2de_add_module(assets ALIAS Assets
    SOURCES engine/src/assets/texture_assets.cpp
    PRIVATE_DEPS IC2DE::Core raylib
    PRIVATE_INCLUDES engine/src
)

ic2de_add_module(animation ALIAS Animation SOURCES engine/src/animation.cpp)

ic2de_add_module(aseprite ALIAS Aseprite
    SOURCES engine/src/aseprite.cpp
    PUBLIC_DEPS IC2DE::Animation
    PRIVATE_DEPS nlohmann_json::nlohmann_json
)

ic2de_add_module(debug ALIAS Debug SOURCES engine/src/debug_visuals.cpp)

ic2de_add_module(aiming ALIAS Aiming SOURCES engine/src/aiming.cpp)

ic2de_add_module(interaction ALIAS Interaction SOURCES engine/src/interaction.cpp)

ic2de_add_module(render ALIAS Render
    SOURCES
        engine/src/projection25d.cpp
        engine/src/render2d.cpp
)

ic2de_add_module(world ALIAS World
    SOURCES engine/src/world.cpp
    PRIVATE_DEPS EnTT::EnTT
)

ic2de_add_module(scene ALIAS Scene
    SOURCES
        engine/src/scene.cpp
        engine/src/scene_document.cpp
        engine/src/scene_editor.cpp
        engine/src/runtime_scene.cpp
    PUBLIC_DEPS
        IC2DE::Animation
        IC2DE::Flow
        IC2DE::Ground
        IC2DE::Interaction
        IC2DE::Jobs
        IC2DE::Locomotion
        IC2DE::Physics
        IC2DE::World
    PRIVATE_DEPS
        IC2DE::Aseprite
        IC2DE::Assets
        IC2DE::Render
)

# ---------------------------------------------------------------------------
# Development editor
#
# Dear ImGui backs the development editor only. The shipping preset turns
# IC2DE_ENABLE_DEVELOPMENT_TOOLS off and never fetches, compiles, or links it.
# ---------------------------------------------------------------------------

if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b-docking
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # Third-party sources, so this one is deliberately not an ic2de_add_module:
    # it carries neither the project's warning settings nor its analysis.
    add_library(ic2de_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    )
    target_include_directories(ic2de_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
    target_compile_features(ic2de_imgui PUBLIC cxx_std_20)

    ic2de_add_module(editor ALIAS Editor
        SOURCES
            engine/src/editor/editor_camera.cpp
            engine/src/editor/editor_layout.cpp
            engine/src/editor/editor_shell.cpp
            engine/src/editor/editor_ui.cpp
            engine/src/editor/imgui_raylib_backend.cpp
        PUBLIC_DEPS
            IC2DE::ActorDebug
            IC2DE::Aiming
            IC2DE::Debug
            IC2DE::Interaction
            IC2DE::Render
            IC2DE::Scene
        PRIVATE_DEPS
            IC2DE::Combat
            IC2DE::Core
            IC2DE::EnemyIntent
            IC2DE::Health
            IC2DE::Input
            IC2DE::NavAgent
            IC2DE::NavPathfinding
            IC2DE::Projectiles
            ic2de_imgui
            raylib
        PRIVATE_INCLUDES engine/src
    )
endif()

# ---------------------------------------------------------------------------
# Runtime
# ---------------------------------------------------------------------------

ic2de_add_module(runtime ALIAS Runtime
    SOURCES
        engine/src/application.cpp
        engine/src/core/renderdoc_capture.cpp
        engine/src/input/raylib_input_adapter.cpp
        engine/src/render/frame_pipeline.cpp
        engine/src/render/gpu_backdrop.cpp
        engine/src/render/raylib_renderer2d.cpp
    PUBLIC_DEPS IC2DE::Core
    PRIVATE_DEPS
        IC2DE::ActorDebug
        IC2DE::Aiming
        IC2DE::Assets
        IC2DE::Combat
        IC2DE::Crowd
        IC2DE::Interaction
        IC2DE::Debug
        IC2DE::FlowField
        IC2DE::EnemyIntent
        IC2DE::GameplayState
        IC2DE::Ground
        IC2DE::Health
        IC2DE::Input
        IC2DE::Jobs
        IC2DE::NavAgent
        IC2DE::NavGrid
        IC2DE::NavPathfinding
        IC2DE::Physics
        IC2DE::Presentation
        IC2DE::Project
        IC2DE::Projectiles
        IC2DE::Render
        IC2DE::Scene
        IC2DE::World
        raylib
    PRIVATE_INCLUDES engine/src
)
if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
    target_link_libraries(ic2de_runtime PRIVATE IC2DE::Editor)
endif()
target_compile_definitions(ic2de_runtime PRIVATE
    IC2DE_ENABLE_DEVELOPMENT_TOOLS=$<BOOL:${IC2DE_ENABLE_DEVELOPMENT_TOOLS}>
)

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------

if(IC2DE_BUILD_TESTBED)
    ic2de_add_executable(testbed SOURCES game/src/main.cpp DEPS IC2DE::Runtime)

    set_target_properties(ic2de_testbed PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    file(GLOB_RECURSE IC2DE_TESTBED_RUNTIME_CONTENT CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/game/assets/runtime/*"
    )
    set(IC2DE_TESTBED_CONTENT_STAMP
        "${CMAKE_CURRENT_BINARY_DIR}/ic2de-testbed-content.stamp"
    )
    add_custom_command(
        OUTPUT "${IC2DE_TESTBED_CONTENT_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Content"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/game/runtime/IC_2DE.runtime"
                "${CMAKE_CURRENT_BINARY_DIR}/IC_2DE.runtime"
        # Preserve content subdirectories: shader and future streaming paths
        # are part of runtime identity rather than flattenable filenames.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/game/assets/runtime"
                "${CMAKE_CURRENT_BINARY_DIR}/Content"
        COMMAND ${CMAKE_COMMAND} -E touch "${IC2DE_TESTBED_CONTENT_STAMP}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/game/runtime/IC_2DE.runtime"
            ${IC2DE_TESTBED_RUNTIME_CONTENT}
        COMMENT "Staging relocatable runtime content beside the IC_2DE testbed"
        VERBATIM
    )
    add_custom_target(ic2de_testbed_content ALL
        DEPENDS "${IC2DE_TESTBED_CONTENT_STAMP}"
    )
    add_dependencies(ic2de_testbed ic2de_testbed_content)

    # The development editor is a first-class build output beside the testbed
    # and the shipping runtime rather than a hidden mode of one of them.
    if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
        ic2de_add_executable(editor_app
            SOURCES game/src/main.cpp
            DEPS IC2DE::Runtime
            DEFINITIONS IC2DE_DEFAULT_START_WITH_EDITOR=1
        )
        set_target_properties(ic2de_editor_app PROPERTIES
            OUTPUT_NAME "IC_2DE-Editor"
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
        add_dependencies(ic2de_editor_app ic2de_testbed_content)
    endif()
endif()

if(IC2DE_BUILD_SHIPPING_RUNTIME)
    ic2de_add_executable(shipping SOURCES game/src/shipping_main.cpp DEPS IC2DE::Runtime)
    set_target_properties(ic2de_shipping PROPERTIES OUTPUT_NAME "IC_2DE")

    install(TARGETS ic2de_shipping RUNTIME DESTINATION . COMPONENT Runtime)
    install(FILES game/runtime/IC_2DE.runtime packaging/RUNTIME_README.txt
                  packaging/THIRD_PARTY_NOTICES.txt DESTINATION . COMPONENT Runtime)
    install(DIRECTORY game/assets/runtime/ DESTINATION Content COMPONENT Runtime)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(IC2DE_BUILD_TESTS)
    include(CTest)
    enable_testing()

    set(IC2DE_TEST_FIXTURES
        IC2DE_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    set(IC2DE_RUNTIME_ASSETS
        IC2DE_RUNTIME_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/game/assets/runtime"
    )

    ic2de_add_test(core
        SOURCES
            tests/unit/automated_run_tests.cpp
            tests/unit/fixed_step_clock_tests.cpp
        DEPS IC2DE::Core
    )

    ic2de_add_test(frame_telemetry
        SOURCES tests/unit/frame_telemetry_tests.cpp
        DEPS IC2DE::Core
    )

    ic2de_add_test(flow SOURCES tests/unit/layer_stack_tests.cpp DEPS IC2DE::Flow)

    ic2de_add_test(input SOURCES tests/unit/input_tests.cpp DEPS IC2DE::Input)

    if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
        ic2de_add_test(editor_input
            SOURCES
                tests/unit/editor_camera_tests.cpp
                tests/unit/editor_input_tests.cpp
            DEPS IC2DE::Editor
            PRIVATE_INCLUDES engine/src
        )
    endif()

    ic2de_add_test(combat SOURCES tests/unit/combat_tests.cpp DEPS IC2DE::Combat)

    ic2de_add_test(enemy_intent
        SOURCES tests/unit/enemy_intent_tests.cpp
        DEPS IC2DE::EnemyIntent
    )

    ic2de_add_test(projectile
        SOURCES tests/unit/projectile_tests.cpp
        DEPS IC2DE::Projectiles
        TEST_NAME ic2de.projectiles
    )

    ic2de_add_test(actor_debug
        SOURCES tests/unit/actor_debug_tests.cpp
        DEPS IC2DE::ActorDebug
    )

    ic2de_add_test(health SOURCES tests/unit/health_tests.cpp DEPS IC2DE::Health)

    ic2de_add_test(gameplay_state
        SOURCES tests/unit/gameplay_state_tests.cpp
        DEPS IC2DE::GameplayState
    )

    ic2de_add_test(locomotion
        SOURCES tests/unit/locomotion_tests.cpp
        DEPS IC2DE::Locomotion
    )

    ic2de_add_test(assets
        SOURCES tests/unit/assets_tests.cpp
        DEPS IC2DE::Assets raylib
        DEFINITIONS ${IC2DE_RUNTIME_ASSETS}
    )

    ic2de_add_test(project
        SOURCES tests/unit/runtime_project_tests.cpp
        DEPS IC2DE::Project
    )

    ic2de_add_test(ground SOURCES tests/unit/ground_map_tests.cpp DEPS IC2DE::Ground)

    ic2de_add_test(nav_grid SOURCES tests/unit/nav_grid_tests.cpp DEPS IC2DE::NavGrid)

    ic2de_add_test(nav_pathfinding
        SOURCES tests/unit/nav_pathfinding_tests.cpp
        DEPS IC2DE::NavPathfinding
    )

    ic2de_add_test(nav_agent SOURCES tests/unit/nav_agent_tests.cpp DEPS IC2DE::NavAgent)

    ic2de_add_test(flow_field
        SOURCES tests/unit/flow_field_tests.cpp
        DEPS IC2DE::FlowField IC2DE::NavPathfinding
    )

    ic2de_add_test(crowd
        SOURCES tests/unit/crowd_separation_tests.cpp
        DEPS IC2DE::Crowd
        TEST_NAME ic2de.crowd_separation
    )

    ic2de_add_test(physics SOURCES tests/unit/physics2d_tests.cpp DEPS IC2DE::Physics)

    ic2de_add_test(animation SOURCES tests/unit/animation_tests.cpp DEPS IC2DE::Animation)

    ic2de_add_test(aseprite
        SOURCES tests/unit/aseprite_tests.cpp
        DEPS IC2DE::Aseprite
        DEFINITIONS ${IC2DE_TEST_FIXTURES}
    )

    ic2de_add_test(scene
        SOURCES
            tests/unit/scene_tests.cpp
            tests/unit/scene_editor_tests.cpp
        DEPS IC2DE::Scene
        DEFINITIONS ${IC2DE_TEST_FIXTURES} ${IC2DE_RUNTIME_ASSETS}
    )

    ic2de_add_test(presentation
        SOURCES tests/unit/presentation_tests.cpp
        DEPS IC2DE::Presentation
    )

    ic2de_add_test(jobs SOURCES tests/unit/jobs_tests.cpp DEPS IC2DE::Jobs)

    ic2de_add_test(world SOURCES tests/unit/world_tests.cpp DEPS IC2DE::World)

    ic2de_add_test(debug SOURCES tests/unit/debug_visuals_tests.cpp DEPS IC2DE::Debug)

    ic2de_add_test(aiming SOURCES tests/unit/aiming_tests.cpp DEPS IC2DE::Aiming)

    ic2de_add_test(interaction
        SOURCES tests/unit/interaction_tests.cpp
        DEPS IC2DE::Combat IC2DE::Interaction
    )

    ic2de_add_test(render
        SOURCES
            tests/unit/projection25d_tests.cpp
            tests/unit/render2d_tests.cpp
        DEPS IC2DE::Render
    )

    # Tests that drive a real executable. Each opens a window, so all of them
    # carry the "gpu" label applied by ic2de_add_smoke_test.
    if(TARGET ic2de_testbed)
        ic2de_add_smoke_test(ic2de.testbed_startup
            TARGET ic2de_testbed
            ARGS --validate-content
        )
    endif()

    if(TARGET ic2de_editor_app)
        # Crowd actors hold no rigid body, so nothing in the unit suite can
        # cover their being shot: it needs a real scene, projectiles and health.
        ic2de_add_smoke_test(ic2de.crowd_kill
            TARGET ic2de_editor_app
            ARGS --smoke-crowd-kill
        )

        # Reviving a retired actor has to restore everything it needs to be
        # shot again, not only its presentation. That is only observable across
        # a restart, so it needs a real scene and a second kill.
        ic2de_add_smoke_test(ic2de.restart_recovery
            TARGET ic2de_editor_app
            ARGS --smoke-restart-recovery
        )
    endif()
endif()
