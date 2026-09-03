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

add_library(ic2de_core STATIC
    engine/src/core/automated_run.cpp
    engine/src/core/fixed_step_clock.cpp
    engine/src/core/frame_telemetry.cpp
    engine/src/core/log.cpp
)
add_library(IC2DE::Core ALIAS ic2de_core)
target_include_directories(ic2de_core PUBLIC engine/include)
target_compile_features(ic2de_core PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_core)

add_library(ic2de_flow STATIC
    engine/src/layer_stack.cpp
)
add_library(IC2DE::Flow ALIAS ic2de_flow)
target_include_directories(ic2de_flow PUBLIC engine/include)
target_compile_features(ic2de_flow PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_flow)

add_library(ic2de_jobs STATIC
    engine/src/jobs.cpp
)
add_library(IC2DE::Jobs ALIAS ic2de_jobs)
target_include_directories(ic2de_jobs PUBLIC engine/include)
target_compile_features(ic2de_jobs PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_jobs)

add_library(ic2de_project STATIC
    engine/src/runtime_project.cpp
)
add_library(IC2DE::Project ALIAS ic2de_project)
target_include_directories(ic2de_project PUBLIC engine/include)
target_compile_features(ic2de_project PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_project)

add_library(ic2de_ground STATIC
    engine/src/ground_map.cpp
)
add_library(IC2DE::Ground ALIAS ic2de_ground)
target_include_directories(ic2de_ground PUBLIC engine/include)
target_compile_features(ic2de_ground PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_ground)

add_library(ic2de_nav_grid STATIC
    engine/src/nav_grid.cpp
)
add_library(IC2DE::NavGrid ALIAS ic2de_nav_grid)
target_include_directories(ic2de_nav_grid PUBLIC engine/include)
target_link_libraries(ic2de_nav_grid PUBLIC IC2DE::Ground)
target_compile_features(ic2de_nav_grid PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_nav_grid)

add_library(ic2de_nav_pathfinding STATIC
    engine/src/nav_pathfinding.cpp
)
add_library(IC2DE::NavPathfinding ALIAS ic2de_nav_pathfinding)
target_include_directories(ic2de_nav_pathfinding PUBLIC engine/include)
target_link_libraries(ic2de_nav_pathfinding PUBLIC IC2DE::NavGrid)
target_compile_features(ic2de_nav_pathfinding PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_nav_pathfinding)

add_library(ic2de_nav_agent STATIC
    engine/src/nav_agent.cpp
)
add_library(IC2DE::NavAgent ALIAS ic2de_nav_agent)
target_include_directories(ic2de_nav_agent PUBLIC engine/include)
target_link_libraries(ic2de_nav_agent PUBLIC IC2DE::NavPathfinding)
target_compile_features(ic2de_nav_agent PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_nav_agent)

add_library(ic2de_flow_field STATIC
    engine/src/flow_field.cpp
)
add_library(IC2DE::FlowField ALIAS ic2de_flow_field)
target_include_directories(ic2de_flow_field PUBLIC engine/include)
target_link_libraries(ic2de_flow_field PUBLIC IC2DE::NavGrid)
target_compile_features(ic2de_flow_field PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_flow_field)

add_library(ic2de_crowd STATIC
    engine/src/crowd_separation.cpp
)
add_library(IC2DE::Crowd ALIAS ic2de_crowd)
target_include_directories(ic2de_crowd PUBLIC engine/include)
target_link_libraries(ic2de_crowd PUBLIC IC2DE::Jobs)
target_compile_features(ic2de_crowd PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_crowd)

add_library(ic2de_physics STATIC
    engine/src/physics2d.cpp
)
add_library(IC2DE::Physics ALIAS ic2de_physics)
target_include_directories(ic2de_physics PUBLIC engine/include)
target_link_libraries(ic2de_physics PRIVATE box2d::box2d)
target_compile_features(ic2de_physics PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_physics)

add_library(ic2de_input STATIC
    engine/src/input.cpp
)
add_library(IC2DE::Input ALIAS ic2de_input)
target_include_directories(ic2de_input PUBLIC engine/include)
target_compile_features(ic2de_input PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_input)

add_library(ic2de_combat STATIC
    engine/src/combat.cpp
)
add_library(IC2DE::Combat ALIAS ic2de_combat)
target_include_directories(ic2de_combat PUBLIC engine/include)
target_compile_features(ic2de_combat PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_combat)

add_library(ic2de_enemy_intent STATIC
    engine/src/enemy_intent.cpp
)
add_library(IC2DE::EnemyIntent ALIAS ic2de_enemy_intent)
target_include_directories(ic2de_enemy_intent PUBLIC engine/include)
target_compile_features(ic2de_enemy_intent PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_enemy_intent)

add_library(ic2de_projectiles STATIC
    engine/src/projectiles.cpp
)
add_library(IC2DE::Projectiles ALIAS ic2de_projectiles)
target_include_directories(ic2de_projectiles PUBLIC engine/include)
target_link_libraries(ic2de_projectiles PUBLIC IC2DE::Combat)
target_compile_features(ic2de_projectiles PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_projectiles)

add_library(ic2de_actor_debug STATIC
    engine/src/actor_debug.cpp
)
add_library(IC2DE::ActorDebug ALIAS ic2de_actor_debug)
target_include_directories(ic2de_actor_debug PUBLIC engine/include)
target_compile_features(ic2de_actor_debug PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_actor_debug)

add_library(ic2de_health STATIC
    engine/src/health.cpp
)
add_library(IC2DE::Health ALIAS ic2de_health)
target_include_directories(ic2de_health PUBLIC engine/include)
target_compile_features(ic2de_health PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_health)

add_library(ic2de_gameplay_state STATIC
    engine/src/gameplay_state.cpp
)
add_library(IC2DE::GameplayState ALIAS ic2de_gameplay_state)
target_include_directories(ic2de_gameplay_state PUBLIC engine/include)
target_link_libraries(ic2de_gameplay_state
    PUBLIC IC2DE::Combat IC2DE::EnemyIntent IC2DE::Health IC2DE::NavAgent IC2DE::Projectiles IC2DE::World
)
target_compile_features(ic2de_gameplay_state PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_gameplay_state)

add_library(ic2de_presentation STATIC
    engine/src/presentation.cpp
)
add_library(IC2DE::Presentation ALIAS ic2de_presentation)
target_include_directories(ic2de_presentation PUBLIC engine/include)
target_compile_features(ic2de_presentation PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_presentation)

add_library(ic2de_locomotion STATIC
    engine/src/locomotion.cpp
)
add_library(IC2DE::Locomotion ALIAS ic2de_locomotion)
target_include_directories(ic2de_locomotion PUBLIC engine/include)
target_compile_features(ic2de_locomotion PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_locomotion)

add_library(ic2de_assets STATIC
    engine/src/assets/texture_assets.cpp
)
add_library(IC2DE::Assets ALIAS ic2de_assets)
target_include_directories(ic2de_assets PUBLIC engine/include)
target_include_directories(ic2de_assets PRIVATE engine/src)
target_link_libraries(ic2de_assets PRIVATE IC2DE::Core raylib)
target_compile_features(ic2de_assets PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_assets)

add_library(ic2de_animation STATIC
    engine/src/animation.cpp
)
add_library(IC2DE::Animation ALIAS ic2de_animation)
target_include_directories(ic2de_animation PUBLIC engine/include)
target_compile_features(ic2de_animation PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_animation)

add_library(ic2de_aseprite STATIC
    engine/src/aseprite.cpp
)
add_library(IC2DE::Aseprite ALIAS ic2de_aseprite)
target_include_directories(ic2de_aseprite PUBLIC engine/include)
target_link_libraries(ic2de_aseprite
    PUBLIC IC2DE::Animation
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(ic2de_aseprite PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_aseprite)

add_library(ic2de_debug STATIC
    engine/src/debug_visuals.cpp
)
add_library(IC2DE::Debug ALIAS ic2de_debug)
target_include_directories(ic2de_debug PUBLIC engine/include)
target_compile_features(ic2de_debug PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_debug)

add_library(ic2de_aiming STATIC
    engine/src/aiming.cpp
)
add_library(IC2DE::Aiming ALIAS ic2de_aiming)
target_include_directories(ic2de_aiming PUBLIC engine/include)
target_compile_features(ic2de_aiming PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_aiming)

add_library(ic2de_interaction STATIC
    engine/src/interaction.cpp
)
add_library(IC2DE::Interaction ALIAS ic2de_interaction)
target_include_directories(ic2de_interaction PUBLIC engine/include)
target_compile_features(ic2de_interaction PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_interaction)

add_library(ic2de_render STATIC
    engine/src/projection25d.cpp
    engine/src/render2d.cpp
)
add_library(IC2DE::Render ALIAS ic2de_render)
target_include_directories(ic2de_render PUBLIC engine/include)
target_compile_features(ic2de_render PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_render)

add_library(ic2de_world STATIC
    engine/src/world.cpp
)
add_library(IC2DE::World ALIAS ic2de_world)
target_include_directories(ic2de_world PUBLIC engine/include)
target_link_libraries(ic2de_world PRIVATE EnTT::EnTT)
target_compile_features(ic2de_world PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_world)

add_library(ic2de_scene STATIC
    engine/src/scene.cpp
    engine/src/scene_document.cpp
    engine/src/scene_editor.cpp
    engine/src/runtime_scene.cpp
)
add_library(IC2DE::Scene ALIAS ic2de_scene)
target_include_directories(ic2de_scene PUBLIC engine/include)
target_link_libraries(ic2de_scene
    PUBLIC IC2DE::Animation IC2DE::Flow IC2DE::Ground IC2DE::Interaction IC2DE::Jobs IC2DE::Locomotion IC2DE::Physics IC2DE::World
    PRIVATE IC2DE::Aseprite IC2DE::Assets IC2DE::Render
)
target_compile_features(ic2de_scene PUBLIC cxx_std_20)
ic2de_set_project_warnings(ic2de_scene)

# Dear ImGui backs the development editor only. The shipping preset turns
# IC2DE_ENABLE_DEVELOPMENT_TOOLS off and never fetches, compiles, or links it.
if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b-docking
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(ic2de_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    )
    target_include_directories(ic2de_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
    target_compile_features(ic2de_imgui PUBLIC cxx_std_20)

    add_library(ic2de_editor STATIC
        engine/src/editor/editor_camera.cpp
        engine/src/editor/editor_layout.cpp
        engine/src/editor/editor_shell.cpp
        engine/src/editor/editor_ui.cpp
        engine/src/editor/imgui_raylib_backend.cpp
    )
    add_library(IC2DE::Editor ALIAS ic2de_editor)
    target_include_directories(ic2de_editor PUBLIC engine/include)
    target_include_directories(ic2de_editor PRIVATE engine/src)
    target_link_libraries(ic2de_editor
        PUBLIC IC2DE::ActorDebug IC2DE::Aiming IC2DE::Debug IC2DE::Interaction IC2DE::Render IC2DE::Scene
        PRIVATE IC2DE::Combat IC2DE::Core IC2DE::EnemyIntent IC2DE::Health IC2DE::Input IC2DE::NavAgent IC2DE::NavPathfinding IC2DE::Projectiles ic2de_imgui raylib
    )
    target_compile_features(ic2de_editor PUBLIC cxx_std_20)
    ic2de_set_project_warnings(ic2de_editor)
endif()

add_library(ic2de_runtime STATIC
    engine/src/application.cpp
    engine/src/core/renderdoc_capture.cpp
    engine/src/input/raylib_input_adapter.cpp
    engine/src/render/frame_pipeline.cpp
    engine/src/render/gpu_backdrop.cpp
    engine/src/render/raylib_renderer2d.cpp
)
add_library(IC2DE::Runtime ALIAS ic2de_runtime)
target_include_directories(ic2de_runtime PUBLIC engine/include)
target_include_directories(ic2de_runtime PRIVATE engine/src)
target_link_libraries(ic2de_runtime
    PUBLIC IC2DE::Core
    PRIVATE IC2DE::ActorDebug IC2DE::Aiming IC2DE::Assets IC2DE::Combat IC2DE::Crowd IC2DE::Interaction IC2DE::Debug IC2DE::FlowField IC2DE::EnemyIntent IC2DE::GameplayState IC2DE::Ground IC2DE::Health IC2DE::Input IC2DE::Jobs IC2DE::NavAgent IC2DE::NavGrid IC2DE::NavPathfinding IC2DE::Physics IC2DE::Presentation IC2DE::Project IC2DE::Projectiles IC2DE::Render IC2DE::Scene IC2DE::World raylib
)
if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
    target_link_libraries(ic2de_runtime PRIVATE IC2DE::Editor)
endif()
target_compile_features(ic2de_runtime PUBLIC cxx_std_20)
target_compile_definitions(ic2de_runtime PRIVATE
    IC2DE_ENABLE_DEVELOPMENT_TOOLS=$<BOOL:${IC2DE_ENABLE_DEVELOPMENT_TOOLS}>
)
ic2de_set_project_warnings(ic2de_runtime)

if(IC2DE_BUILD_TESTBED)
    add_executable(ic2de_testbed game/src/main.cpp)
    target_link_libraries(ic2de_testbed PRIVATE IC2DE::Runtime)
    target_compile_features(ic2de_testbed PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_testbed)

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
        add_executable(ic2de_editor_app game/src/main.cpp)
        target_link_libraries(ic2de_editor_app PRIVATE IC2DE::Runtime)
        target_compile_features(ic2de_editor_app PRIVATE cxx_std_20)
        target_compile_definitions(ic2de_editor_app PRIVATE
            IC2DE_DEFAULT_START_WITH_EDITOR=1
        )
        ic2de_set_project_warnings(ic2de_editor_app)
        set_target_properties(ic2de_editor_app PROPERTIES
            OUTPUT_NAME "IC_2DE-Editor"
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
        add_dependencies(ic2de_editor_app ic2de_testbed_content)
    endif()
endif()

if(IC2DE_BUILD_SHIPPING_RUNTIME)
    add_executable(ic2de_shipping game/src/shipping_main.cpp)
    target_link_libraries(ic2de_shipping PRIVATE IC2DE::Runtime)
    target_compile_features(ic2de_shipping PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_shipping)
    set_target_properties(ic2de_shipping PROPERTIES OUTPUT_NAME "IC_2DE")

    install(TARGETS ic2de_shipping RUNTIME DESTINATION . COMPONENT Runtime)
    install(FILES game/runtime/IC_2DE.runtime packaging/RUNTIME_README.txt
                  packaging/THIRD_PARTY_NOTICES.txt DESTINATION . COMPONENT Runtime)
    install(DIRECTORY game/assets/runtime/ DESTINATION Content COMPONENT Runtime)
endif()

if(IC2DE_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(ic2de_core_tests
        tests/unit/automated_run_tests.cpp
        tests/unit/fixed_step_clock_tests.cpp
    )
    target_link_libraries(ic2de_core_tests PRIVATE IC2DE::Core)
    target_compile_features(ic2de_core_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_core_tests)

    add_test(NAME ic2de.core COMMAND ic2de_core_tests)

    add_executable(ic2de_frame_telemetry_tests
        tests/unit/frame_telemetry_tests.cpp
    )
    target_link_libraries(ic2de_frame_telemetry_tests PRIVATE IC2DE::Core)
    target_compile_features(ic2de_frame_telemetry_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_frame_telemetry_tests)
    add_test(NAME ic2de.frame_telemetry COMMAND ic2de_frame_telemetry_tests)

    add_executable(ic2de_flow_tests
        tests/unit/layer_stack_tests.cpp
    )
    target_link_libraries(ic2de_flow_tests PRIVATE IC2DE::Flow)
    target_compile_features(ic2de_flow_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_flow_tests)
    add_test(NAME ic2de.flow COMMAND ic2de_flow_tests)

    add_executable(ic2de_input_tests
        tests/unit/input_tests.cpp
    )
    target_link_libraries(ic2de_input_tests PRIVATE IC2DE::Input)
    target_compile_features(ic2de_input_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_input_tests)
    add_test(NAME ic2de.input COMMAND ic2de_input_tests)

    if(IC2DE_ENABLE_DEVELOPMENT_TOOLS)
        add_executable(ic2de_editor_input_tests
            tests/unit/editor_camera_tests.cpp
            tests/unit/editor_input_tests.cpp
        )
        target_link_libraries(ic2de_editor_input_tests PRIVATE IC2DE::Editor)
        target_include_directories(ic2de_editor_input_tests PRIVATE engine/src)
        target_compile_features(ic2de_editor_input_tests PRIVATE cxx_std_20)
        ic2de_set_project_warnings(ic2de_editor_input_tests)
        add_test(NAME ic2de.editor_input COMMAND ic2de_editor_input_tests)
    endif()

    add_executable(ic2de_combat_tests
        tests/unit/combat_tests.cpp
    )
    target_link_libraries(ic2de_combat_tests PRIVATE IC2DE::Combat)
    target_compile_features(ic2de_combat_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_combat_tests)
    add_test(NAME ic2de.combat COMMAND ic2de_combat_tests)

    add_executable(ic2de_enemy_intent_tests
        tests/unit/enemy_intent_tests.cpp
    )
    target_link_libraries(ic2de_enemy_intent_tests PRIVATE IC2DE::EnemyIntent)
    target_compile_features(ic2de_enemy_intent_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_enemy_intent_tests)
    add_test(NAME ic2de.enemy_intent COMMAND ic2de_enemy_intent_tests)

    add_executable(ic2de_projectile_tests
        tests/unit/projectile_tests.cpp
    )
    target_link_libraries(ic2de_projectile_tests PRIVATE IC2DE::Projectiles)
    target_compile_features(ic2de_projectile_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_projectile_tests)
    add_test(NAME ic2de.projectiles COMMAND ic2de_projectile_tests)

    add_executable(ic2de_actor_debug_tests
        tests/unit/actor_debug_tests.cpp
    )
    target_link_libraries(ic2de_actor_debug_tests PRIVATE IC2DE::ActorDebug)
    target_compile_features(ic2de_actor_debug_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_actor_debug_tests)
    add_test(NAME ic2de.actor_debug COMMAND ic2de_actor_debug_tests)

    add_executable(ic2de_health_tests
        tests/unit/health_tests.cpp
    )
    target_link_libraries(ic2de_health_tests PRIVATE IC2DE::Health)
    target_compile_features(ic2de_health_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_health_tests)
    add_test(NAME ic2de.health COMMAND ic2de_health_tests)

    add_executable(ic2de_gameplay_state_tests
        tests/unit/gameplay_state_tests.cpp
    )
    target_link_libraries(ic2de_gameplay_state_tests
        PRIVATE IC2DE::GameplayState
    )
    target_compile_features(ic2de_gameplay_state_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_gameplay_state_tests)
    add_test(NAME ic2de.gameplay_state COMMAND ic2de_gameplay_state_tests)

    add_executable(ic2de_locomotion_tests
        tests/unit/locomotion_tests.cpp
    )
    target_link_libraries(ic2de_locomotion_tests PRIVATE IC2DE::Locomotion)
    target_compile_features(ic2de_locomotion_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_locomotion_tests)
    add_test(NAME ic2de.locomotion COMMAND ic2de_locomotion_tests)

    add_executable(ic2de_assets_tests
        tests/unit/assets_tests.cpp
    )
    target_link_libraries(ic2de_assets_tests PRIVATE IC2DE::Assets raylib)
    target_compile_features(ic2de_assets_tests PRIVATE cxx_std_20)
    target_compile_definitions(ic2de_assets_tests PRIVATE
        IC2DE_RUNTIME_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/game/assets/runtime"
    )
    ic2de_set_project_warnings(ic2de_assets_tests)
    add_test(NAME ic2de.assets COMMAND ic2de_assets_tests)

    add_executable(ic2de_project_tests
        tests/unit/runtime_project_tests.cpp
    )
    target_link_libraries(ic2de_project_tests PRIVATE IC2DE::Project)
    target_compile_features(ic2de_project_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_project_tests)
    add_test(NAME ic2de.project COMMAND ic2de_project_tests)

    add_executable(ic2de_ground_tests
        tests/unit/ground_map_tests.cpp
    )
    target_link_libraries(ic2de_ground_tests PRIVATE IC2DE::Ground)
    target_compile_features(ic2de_ground_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_ground_tests)
    add_test(NAME ic2de.ground COMMAND ic2de_ground_tests)

    add_executable(ic2de_nav_grid_tests
        tests/unit/nav_grid_tests.cpp
    )
    target_link_libraries(ic2de_nav_grid_tests PRIVATE IC2DE::NavGrid)
    target_compile_features(ic2de_nav_grid_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_nav_grid_tests)
    add_test(NAME ic2de.nav_grid COMMAND ic2de_nav_grid_tests)

    add_executable(ic2de_nav_pathfinding_tests
        tests/unit/nav_pathfinding_tests.cpp
    )
    target_link_libraries(ic2de_nav_pathfinding_tests PRIVATE IC2DE::NavPathfinding)
    target_compile_features(ic2de_nav_pathfinding_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_nav_pathfinding_tests)
    add_test(NAME ic2de.nav_pathfinding COMMAND ic2de_nav_pathfinding_tests)

    add_executable(ic2de_nav_agent_tests
        tests/unit/nav_agent_tests.cpp
    )
    target_link_libraries(ic2de_nav_agent_tests PRIVATE IC2DE::NavAgent)
    target_compile_features(ic2de_nav_agent_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_nav_agent_tests)
    add_test(NAME ic2de.nav_agent COMMAND ic2de_nav_agent_tests)

    add_executable(ic2de_flow_field_tests
        tests/unit/flow_field_tests.cpp
    )
    target_link_libraries(ic2de_flow_field_tests PRIVATE IC2DE::FlowField IC2DE::NavPathfinding)
    target_compile_features(ic2de_flow_field_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_flow_field_tests)
    add_test(NAME ic2de.flow_field COMMAND ic2de_flow_field_tests)

    add_executable(ic2de_crowd_tests
        tests/unit/crowd_separation_tests.cpp
    )
    target_link_libraries(ic2de_crowd_tests PRIVATE IC2DE::Crowd)
    target_compile_features(ic2de_crowd_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_crowd_tests)
    add_test(NAME ic2de.crowd_separation COMMAND ic2de_crowd_tests)

    add_executable(ic2de_physics_tests
        tests/unit/physics2d_tests.cpp
    )
    target_link_libraries(ic2de_physics_tests PRIVATE IC2DE::Physics)
    target_compile_features(ic2de_physics_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_physics_tests)
    add_test(NAME ic2de.physics COMMAND ic2de_physics_tests)

    add_executable(ic2de_animation_tests
        tests/unit/animation_tests.cpp
    )
    target_link_libraries(ic2de_animation_tests PRIVATE IC2DE::Animation)
    target_compile_features(ic2de_animation_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_animation_tests)
    add_test(NAME ic2de.animation COMMAND ic2de_animation_tests)

    add_executable(ic2de_aseprite_tests
        tests/unit/aseprite_tests.cpp
    )
    target_link_libraries(ic2de_aseprite_tests PRIVATE IC2DE::Aseprite)
    target_compile_features(ic2de_aseprite_tests PRIVATE cxx_std_20)
    target_compile_definitions(ic2de_aseprite_tests PRIVATE
        IC2DE_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    ic2de_set_project_warnings(ic2de_aseprite_tests)
    add_test(NAME ic2de.aseprite COMMAND ic2de_aseprite_tests)

    add_executable(ic2de_scene_tests
        tests/unit/scene_tests.cpp
        tests/unit/scene_editor_tests.cpp
    )
    target_link_libraries(ic2de_scene_tests PRIVATE IC2DE::Scene)
    target_compile_features(ic2de_scene_tests PRIVATE cxx_std_20)
    target_compile_definitions(ic2de_scene_tests PRIVATE
        IC2DE_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        IC2DE_RUNTIME_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/game/assets/runtime"
    )
    ic2de_set_project_warnings(ic2de_scene_tests)
    add_test(NAME ic2de.scene COMMAND ic2de_scene_tests)

    add_executable(ic2de_presentation_tests
        tests/unit/presentation_tests.cpp
    )
    target_link_libraries(ic2de_presentation_tests PRIVATE IC2DE::Presentation)
    target_compile_features(ic2de_presentation_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_presentation_tests)
    add_test(NAME ic2de.presentation COMMAND ic2de_presentation_tests)

    add_executable(ic2de_jobs_tests
        tests/unit/jobs_tests.cpp
    )
    target_link_libraries(ic2de_jobs_tests PRIVATE IC2DE::Jobs)
    target_compile_features(ic2de_jobs_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_jobs_tests)
    add_test(NAME ic2de.jobs COMMAND ic2de_jobs_tests)

    add_executable(ic2de_world_tests
        tests/unit/world_tests.cpp
    )
    target_link_libraries(ic2de_world_tests PRIVATE IC2DE::World)
    target_compile_features(ic2de_world_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_world_tests)
    add_test(NAME ic2de.world COMMAND ic2de_world_tests)

    add_executable(ic2de_debug_tests
        tests/unit/debug_visuals_tests.cpp
    )
    target_link_libraries(ic2de_debug_tests PRIVATE IC2DE::Debug)
    target_compile_features(ic2de_debug_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_debug_tests)
    add_test(NAME ic2de.debug COMMAND ic2de_debug_tests)

    add_executable(ic2de_aiming_tests
        tests/unit/aiming_tests.cpp
    )
    target_link_libraries(ic2de_aiming_tests PRIVATE IC2DE::Aiming)
    target_compile_features(ic2de_aiming_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_aiming_tests)
    add_test(NAME ic2de.aiming COMMAND ic2de_aiming_tests)

    add_executable(ic2de_interaction_tests
        tests/unit/interaction_tests.cpp
    )
    target_link_libraries(ic2de_interaction_tests PRIVATE IC2DE::Combat IC2DE::Interaction)
    target_compile_features(ic2de_interaction_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_interaction_tests)
    add_test(NAME ic2de.interaction COMMAND ic2de_interaction_tests)

    add_executable(ic2de_render_tests
        tests/unit/projection25d_tests.cpp
        tests/unit/render2d_tests.cpp
    )
    target_link_libraries(ic2de_render_tests PRIVATE IC2DE::Render)
    target_compile_features(ic2de_render_tests PRIVATE cxx_std_20)
    ic2de_set_project_warnings(ic2de_render_tests)
    add_test(NAME ic2de.render COMMAND ic2de_render_tests)

    if(TARGET ic2de_testbed)
        add_test(NAME ic2de.testbed_startup COMMAND ic2de_testbed --validate-content)
        set_tests_properties(ic2de.testbed_startup PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:ic2de_testbed>"
        )
    endif()

    # Crowd actors hold no rigid body, so nothing in the unit suite can cover
    # their being shot: it needs a real scene, projectiles and health. This
    # opens a window, so it is only registered where the editor build exists.
    if(TARGET ic2de_editor_app)
        add_test(NAME ic2de.crowd_kill COMMAND ic2de_editor_app --smoke-crowd-kill)
        set_tests_properties(ic2de.crowd_kill PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:ic2de_editor_app>"
        )

        # Reviving a retired actor has to restore everything it needs to be
        # shot again, not only its presentation. That is only observable across
        # a restart, so it needs a real scene and a second kill.
        add_test(NAME ic2de.restart_recovery
                 COMMAND ic2de_editor_app --smoke-restart-recovery)
        set_tests_properties(ic2de.restart_recovery PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:ic2de_editor_app>"
        )
    endif()
endif()
