#include "ic2d/application.hpp"

#include "ic2d/assets.hpp"
#include "ic2d/core/fixed_step_clock.hpp"
#include "ic2d/core/log.hpp"
#include "ic2d/input.hpp"
#include "ic2d/debug_visuals.hpp"
#include "ic2d/jobs.hpp"
#include "ic2d/layer_stack.hpp"
#include "ic2d/presentation.hpp"
#include "ic2d/projection25d.hpp"
#include "ic2d/render2d.hpp"
#include "ic2d/runtime_scene.hpp"
#include "ic2d/runtime_project.hpp"
#include "ic2d/scene.hpp"
#include "input/raylib_input_adapter.hpp"
#include "render/gpu_backdrop.hpp"
#include "render/raylib_renderer2d.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <raylib.h>

#ifndef IC2DE_ENABLE_DEVELOPMENT_TOOLS
#define IC2DE_ENABLE_DEVELOPMENT_TOOLS 1
#endif

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
#include "ic2d/editor.hpp"
#include "ic2d/scene_document.hpp"
#include "ic2d/scene_editor.hpp"
#endif

namespace ic2d {
namespace {

void hash_value(std::uint64_t& hash, const std::uint64_t value) noexcept {
    constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
    for (std::uint32_t byte_index = 0; byte_index < 8; ++byte_index) {
        hash ^= (value >> (byte_index * 8U)) & 0xFFULL;
        hash *= fnv_prime;
    }
}

[[nodiscard]] std::uint64_t replay_state_hash(
    const Vec3& player_position,
    const Vec3& dynamic_prop_position,
    const std::uint64_t simulated_ticks,
    const bool collision_observed,
    const bool elevation_observed,
    const bool trigger_observed,
    const bool physics_contact_observed
) noexcept {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    const auto hash_float = [&hash](const float value) {
        hash_value(hash, std::bit_cast<std::uint32_t>(value));
    };
    hash_float(player_position.x);
    hash_float(player_position.y);
    hash_float(player_position.z);
    hash_float(dynamic_prop_position.x);
    hash_float(dynamic_prop_position.y);
    hash_float(dynamic_prop_position.z);
    hash_value(hash, simulated_ticks);
    hash_value(hash, collision_observed ? 1ULL : 0ULL);
    hash_value(hash, elevation_observed ? 1ULL : 0ULL);
    hash_value(hash, trigger_observed ? 1ULL : 0ULL);
    hash_value(hash, physics_contact_observed ? 1ULL : 0ULL);
    return hash;
}

[[nodiscard]] bool valid(const ApplicationConfig& config) noexcept {
    const bool valid_pacing = config.render_pacing.mode != RenderPacingMode::fixed_hz ||
                              config.render_pacing.fixed_hz > 0;
    return config.window_width > 0 && config.window_height > 0 &&
           config.canvas_width > 0 && config.canvas_height > 0 &&
           valid_pacing && std::isfinite(config.fixed_update_hz) &&
           config.fixed_update_hz > 0.0 && config.max_frames >= 0 && config.capture_frame >= 0;
}

void apply_render_pacing(const RenderPacingConfig& pacing) {
    if (pacing.mode == RenderPacingMode::monitor_synced) {
        SetTargetFPS(0);
        SetWindowState(FLAG_VSYNC_HINT);
        return;
    }

    ClearWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(pacing.mode == RenderPacingMode::fixed_hz ? pacing.fixed_hz : 0);
}

[[nodiscard]] RenderPacingConfig next_render_pacing(const RenderPacingConfig& current) noexcept {
    if (current.mode == RenderPacingMode::uncapped) {
        return {.mode = RenderPacingMode::monitor_synced, .fixed_hz = 0};
    }
    if (current.mode == RenderPacingMode::monitor_synced) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 60};
    }
    if (current.fixed_hz == 60) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 120};
    }
    if (current.fixed_hz == 120) {
        return {.mode = RenderPacingMode::fixed_hz, .fixed_hz = 144};
    }
    return {.mode = RenderPacingMode::uncapped, .fixed_hz = 0};
}

[[nodiscard]] std::string render_pacing_description(const RenderPacingConfig& pacing) {
    if (pacing.mode == RenderPacingMode::uncapped) {
        return "UNLOCKED";
    }
    if (pacing.mode == RenderPacingMode::monitor_synced) {
        const int refresh_hz = GetMonitorRefreshRate(GetCurrentMonitor());
        return "MONITOR " + std::to_string(refresh_hz) + " HZ VSYNC";
    }
    return "CAPPED " + std::to_string(pacing.fixed_hz) + " HZ";
}

[[nodiscard]] bool draw_background(
    const ApplicationConfig& config,
    const bool gpu_background_enabled,
    const GpuBackdrop& gpu_backdrop
) {
    const bool gpu_backdrop_active = gpu_background_enabled && gpu_backdrop.available();
    if (gpu_backdrop_active) {
        gpu_backdrop.draw(config.canvas_width, config.canvas_height);
    } else {
        ClearBackground(Color{18, 24, 36, 255});
    }
    return gpu_backdrop_active;
}

// The first production layer consumes copied simulation events without
// reaching into RuntimeScene. It currently owns smoke/diagnostic observations;
// gameplay and tool layers can use the same typed route later.
class RuntimeObservationLayer final : public Layer {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Runtime observations";
    }

    [[nodiscard]] bool on_event(const EngineEvent& event) override {
        std::visit([this](const auto& typed_event) { observe(typed_event); }, event);
        return false;
    }

    void reset() noexcept {
        physics_contact_observed = false;
        trigger_observed = false;
        animation_event_observed = false;
        animation_identity_observed = false;
        diagonal_animation_observed = false;
    }

    bool physics_contact_observed{false};
    bool trigger_observed{false};
    bool animation_event_observed{false};
    bool animation_identity_observed{false};
    bool diagonal_animation_observed{false};

private:
    void observe(const SceneContactEvent& event) noexcept {
        physics_contact_observed = physics_contact_observed || event.began;
    }

    void observe(const SceneTriggerEvent& event) noexcept {
        trigger_observed = trigger_observed || (event.entered && event.player_visitor);
    }

    void observe(const SceneAnimationEvent& event) noexcept {
        animation_event_observed = true;
        animation_identity_observed = animation_identity_observed ||
                                      static_cast<bool>(event.entity_uuid);
        const bool diagonal_clip =
            event.clip_id == "player-move-southwest" ||
            event.clip_id == "player-move-northwest" ||
            event.clip_id == "player-move-northeast" ||
            event.clip_id == "player-move-southeast";
        diagonal_animation_observed = diagonal_animation_observed ||
                                      (event.entity_id == "player" && diagonal_clip);
    }
};

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
[[nodiscard]] Vector2 canvas_point(
    const ProjectedPoint25D& projected,
    const ApplicationConfig& config
) noexcept {
    return {
        static_cast<float>(config.canvas_width) * 0.5F + projected.position.x,
        static_cast<float>(config.canvas_height) * 0.5F + projected.position.y,
    };
}

void draw_projected_ground_grid(
    const ApplicationConfig& config,
    const Camera25DState& camera,
    const RectXZ& bounds
) {
    constexpr float spacing = 64.0F;
    const float first_x = std::ceil(bounds.x / spacing) * spacing;
    const float first_z = std::ceil(bounds.z / spacing) * spacing;
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    const Color minor{58, 91, 88, 150};
    const Color major{77, 126, 116, 190};

    int index = 0;
    for (float x = first_x; x <= maximum_x; x += spacing, ++index) {
        const auto start = canvas_point(
            project_world_point({x, 0.0F, bounds.z}, camera), config);
        const auto end = canvas_point(
            project_world_point({x, 0.0F, maximum_z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }

    index = 0;
    for (float z = first_z; z <= maximum_z; z += spacing, ++index) {
        const auto start = canvas_point(
            project_world_point({bounds.x, 0.0F, z}, camera), config);
        const auto end = canvas_point(
            project_world_point({maximum_x, 0.0F, z}, camera), config);
        DrawLineV(start, end, index % 4 == 0 ? major : minor);
    }
}
#endif

[[nodiscard]] GroundQuadSubmission2D project_ground_quad(
    const std::uint64_t stable_id,
    const RectXZ& bounds,
    const float elevation,
    const ColorRgba8 tint,
    const Camera25DState& camera
) {
    const float maximum_x = bounds.x + bounds.width;
    const float maximum_z = bounds.z + bounds.depth;
    return {
        .stable_id = stable_id,
        .points = {{
            project_world_point({bounds.x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, bounds.z}, camera).position,
            project_world_point({maximum_x, elevation, maximum_z}, camera).position,
            project_world_point({bounds.x, elevation, maximum_z}, camera).position,
        }},
        .tint = tint,
    };
}

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
[[nodiscard]] GroundQuadSubmission2D project_physics_footprint(
    const PhysicsFootprint& footprint,
    const ColorRgba8 tint,
    const Camera25DState& camera
) {
    const float cosine = std::cos(footprint.rotation_radians);
    const float sine = std::sin(footprint.rotation_radians);
    const std::array<Vec2, 4> local_points{{
        {-footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, -footprint.half_extents.y},
        {footprint.half_extents.x, footprint.half_extents.y},
        {-footprint.half_extents.x, footprint.half_extents.y},
    }};
    GroundQuadSubmission2D projected{
        .stable_id = 50'000ULL +
                     (static_cast<std::uint64_t>(footprint.body.generation) << 32U) +
                     footprint.body.slot,
        .tint = tint,
    };
    for (std::size_t index = 0; index < local_points.size(); ++index) {
        const Vec2 local = local_points[index];
        const float world_x = footprint.center.x + cosine * local.x - sine * local.y;
        const float world_z = footprint.center.y + sine * local.x + cosine * local.y;
        projected.points[index] = project_world_point({world_x, 0.35F, world_z}, camera).position;
    }
    return projected;
}
#endif

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
void draw_overlay(
    const ApplicationConfig& config,
    const bool paused,
    const bool dropped_time,
    const std::uint64_t simulated_ticks,
    const std::string_view pacing_description,
    const bool gpu_backdrop_active,
    const std::size_t cpu_worker_count,
    const std::size_t loaded_texture_count,
    const std::uint32_t scene_schema_version,
    const std::size_t stable_uuid_count,
    const Vec3& player_position,
    const bool movement_blocked,
    const std::optional<std::uint32_t> trigger_tag,
    const bool physics_contact_observed,
    const bool dynamic_prop_moved,
    const Camera25DState& camera,
    const RenderDiagnostics2D& diagnostics,
    const DebugVisuals& debug_visuals,
    const bool editor_open
) {
    DrawText("IC_2DE", 18, 16, 18, Color{74, 222, 190, 255});
    DrawText("2.5D WORLD + ORTHOGRAPHIC BILLBOARDS", 19, 38, 9, Color{166, 178, 198, 255});
    DrawText("Move X/Z: W/A/S/D or arrows", 19, 58, 10, RAYWHITE);
    DrawText("P Pause | O Step | R Reset | F1 Debug | F2 Editor | F6 Hz | G GPU | Esc Quit",
             19, 74, 9, RAYWHITE);
    DrawText(TextFormat("Fixed: %.0f Hz  |  Ticks: %llu", config.fixed_update_hz,
                        static_cast<unsigned long long>(simulated_ticks)),
             19, 90, 10, RAYWHITE);

    DrawText(TextFormat("%d FPS | %s", GetFPS(), pacing_description.data()),
             config.canvas_width - 208, 17, 10, LIME);
    DrawText(TextFormat("CPU workers: %u | GPU backdrop: %s",
                        static_cast<unsigned int>(cpu_worker_count),
                        gpu_backdrop_active ? "ON" : "CPU FALLBACK"),
             config.canvas_width - 208, 34, 9, Color{166, 178, 198, 255});
    DrawText(TextFormat("Ground %u/%u | Sprites %u/%u | Culled %u",
                        static_cast<unsigned int>(diagnostics.visible_ground_quads),
                        static_cast<unsigned int>(diagnostics.submitted_ground_quads),
                        static_cast<unsigned int>(diagnostics.visible_sprites),
                        static_cast<unsigned int>(diagnostics.submitted_sprites),
                        static_cast<unsigned int>(diagnostics.culled_sprites)),
             19, 108, 9, Color{166, 178, 198, 255});
    DrawText(TextFormat("World X %.1f  Y %.1f  Z %.1f | textures %u",
                        player_position.x, player_position.y, player_position.z,
                        static_cast<unsigned int>(loaded_texture_count)),
             19, 123, 9, Color{166, 178, 198, 255});
    DrawText(TextFormat("Collision %s | Trigger %s | Batches %u",
                        movement_blocked ? "BLOCKED" : "CLEAR",
                        trigger_tag ? TextFormat("%u", *trigger_tag) : "NONE",
                        static_cast<unsigned int>(diagnostics.estimated_batches)),
             19, 138, 9, movement_blocked ? ORANGE : Color{166, 178, 198, 255});
    DrawText(TextFormat("Camera X %.1f  Z %.1f | yaw %.0f pitch %.0f deg",
                        camera.focus.x, camera.focus.z, camera.yaw_degrees, camera.pitch_degrees),
             19, 153, 9, Color{166, 178, 198, 255});
    DrawText(TextFormat("Physics contact %s | dynamic prop %s",
                         physics_contact_observed ? "SEEN" : "WAITING",
                         dynamic_prop_moved ? "MOVED" : "STILL"),
             19, 168, 9, physics_contact_observed ? LIME : Color{166, 178, 198, 255});
    DrawText(TextFormat("Scene schema %u | Stable UUIDs %u",
                        static_cast<unsigned int>(scene_schema_version),
                        static_cast<unsigned int>(stable_uuid_count)),
             19, 183, 9, Color{74, 222, 190, 255});
    DrawText(TextFormat("Debug channels %u of %u | Editor %s",
                        static_cast<unsigned int>(debug_visuals.drawn_channel_count()),
                        static_cast<unsigned int>(debug_channel_count),
                        editor_open ? "OPEN" : "CLOSED"),
             19, 198, 9, Color{74, 222, 190, 255});

    if (paused) {
        DrawRectangle(0, 0, config.canvas_width, config.canvas_height, Fade(BLACK, 0.42F));
        DrawText("PAUSED", config.canvas_width / 2 - 43, config.canvas_height / 2 - 18, 22, RAYWHITE);
        DrawText("Press O to advance one simulation tick", config.canvas_width / 2 - 124,
                 config.canvas_height / 2 + 12, 10, Color{248, 194, 89, 255});
    }

    if (dropped_time) {
        DrawText("FRAME STALL CLAMPED", config.canvas_width - 142, 50, 9, ORANGE);
    }

    DrawText("HAZEL IDENTITY + EIGHT-WAY LOCOMOTION", 18, config.canvas_height - 25,
             9, Color{166, 178, 198, 255});
}
#endif

} // namespace

int run_application(const ApplicationConfig& requested_config) {
    if (!valid(requested_config)) {
        log(LogLevel::error, "Invalid application configuration.");
        return 2;
    }

    ApplicationConfig config = requested_config;
    std::optional<RuntimeProject> runtime_project;
    std::optional<SceneDefinition> scene_definition;
    try {
        std::filesystem::path scene_path = config.development_scene_path;
        if (!config.runtime_project_manifest.empty()) {
            runtime_project = RuntimeProject::load(config.runtime_project_manifest);
            config.title = runtime_project->name();
            scene_path = runtime_project->start_scene_path();
            log(LogLevel::info, "Runtime project loaded: " + scene_path.string());
        }
        if (scene_path.empty()) {
            throw std::invalid_argument{"No development or packaged scene path was provided."};
        }
        scene_definition = SceneDefinition::load(scene_path);
        log(LogLevel::info,
            "Authored scene validated: " + scene_definition->id() + " (schema " +
                std::to_string(scene_definition->schema_version()) + ").");
    } catch (const std::exception& error) {
        log(LogLevel::error, std::string{"Runtime content load failed: "} + error.what());
        return 2;
    }
    if (config.validate_content_only) {
        log(LogLevel::info, "Runtime content validation completed without opening a window.");
        return 0;
    }

    unsigned int window_flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE;
    if (config.render_pacing.mode == RenderPacingMode::monitor_synced) {
        window_flags |= FLAG_VSYNC_HINT;
    }
    SetConfigFlags(window_flags);
    InitWindow(config.window_width, config.window_height, config.title.c_str());
    if (!IsWindowReady()) {
        log(LogLevel::error, "Raylib could not create the application window.");
        return 3;
    }

    apply_render_pacing(config.render_pacing);
    log(LogLevel::info, "Application window initialized with " +
                            render_pacing_description(config.render_pacing) + ".");

    RenderTexture2D canvas = LoadRenderTexture(config.canvas_width, config.canvas_height);
    if (canvas.texture.id == 0U) {
        log(LogLevel::error, "Raylib could not create the virtual canvas.");
        CloseWindow();
        return 4;
    }
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);

    const double fixed_step_seconds = 1.0 / config.fixed_update_hz;
    FixedStepClock clock{fixed_step_seconds};
    InputTracker input_tracker;
    RaylibInputAdapter input_adapter;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    JobSystem jobs;
#endif
    TextureAssets textures;
    std::unique_ptr<RuntimeScene> scene;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    const std::uint32_t scene_schema_version = scene_definition->schema_version();
    const std::filesystem::path authored_scene_path = scene_definition->source_path();
    std::size_t stable_uuid_count = 0;
#endif
    try {
        scene = std::make_unique<RuntimeScene>(std::move(*scene_definition), textures);
        const WorldSnapshot initial_world = scene->world_snapshot();
        if (initial_world.entities.size() != scene->entity_count()) {
            throw std::logic_error{"Runtime scene World snapshot is incomplete."};
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        stable_uuid_count = initial_world.entities.size();
#endif
        log(LogLevel::info,
            "Runtime scene instantiated: " + scene->id() + " with " +
                std::to_string(scene->entity_count()) + " entities and " +
                std::to_string(scene->physics_body_count()) + " physics bodies; " +
                std::to_string(scene->animation_binding_count()) + " animated entities; " +
                std::to_string(initial_world.entities.size()) + " stable UUIDs.");
    } catch (const std::exception& error) {
        log(LogLevel::error, std::string{"Runtime scene construction failed: "} + error.what());
        textures.shutdown();
        UnloadRenderTexture(canvas);
        CloseWindow();
        return 5;
    }

    RaylibRenderer2D renderer{textures};
    GpuBackdrop gpu_backdrop;
    RenderQueue2D render_queue;
    Camera25DState world_camera = scene->initial_camera();
    const Camera2DState render_camera{
        .center = {0.0F, 0.0F},
        .rotation_degrees = 0.0F,
        .zoom = 1.0F,
    };
    bool paused = false;
    bool gpu_background_enabled = true;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // The editor and its document are created on first request so an ordinary
    // development or smoke run pays nothing for tools it never opens.
    DebugVisuals debug_visuals;
    debug_visuals.set_enabled(config.start_with_debug_visuals);
    std::optional<EditorShell> editor;
    std::optional<SceneEditor> scene_editor;
#endif
    bool movement_blocked = false;
    std::optional<std::uint32_t> active_trigger;
    bool collision_observed = false;
    bool elevation_observed = false;
    bool dynamic_prop_moved = false;
    LayerStack runtime_layers;
    auto observation_layer = std::make_unique<RuntimeObservationLayer>();
    RuntimeObservationLayer& observations = *observation_layer;
    static_cast<void>(runtime_layers.push_layer(std::move(observation_layer)));
    int rendered_frames = 0;
    std::uint64_t simulated_ticks = 0;
    bool captured_smoke_frame = false;
    bool smoke_capture_failed = false;

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    const auto editor_ready = [&]() {
        if (!editor) {
            editor.emplace();
        }
        if (editor->available() && !scene_editor) {
            try {
                scene_editor.emplace(SceneDocument::open(authored_scene_path));
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Editor could not open the authored scene: "} + error.what());
            }
        }
        return editor->available() && scene_editor.has_value();
    };
    if (config.start_with_editor && editor_ready()) {
        editor->set_visible(true);
        log(LogLevel::info, "Development editor shown.");
    }
#endif

    while (!WindowShouldClose() &&
           (config.max_frames == 0 || rendered_frames < config.max_frames) &&
           (config.max_fixed_ticks == 0 || simulated_ticks < config.max_fixed_ticks)) {
        const double frame_seconds = static_cast<double>(GetFrameTime());
        InputSample input_sample = input_adapter.sample();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (editor && editor->blocks_gameplay_input()) {
            // A panel field is being typed in or dragged, or the pointer and
            // focus are away from the viewport; only the shell and debug
            // toggles still reach the application.
            input_sample = InputSample{
                .toggle_debug_visuals = input_sample.toggle_debug_visuals,
                .toggle_editor = input_sample.toggle_editor,
            };
        }
#endif
        const InputFrame input = input_tracker.update(input_sample);

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (input.pause.pressed) {
            paused = !paused;
            clock.reset();
        }
        const auto forget_observations = [&]() {
            movement_blocked = false;
            active_trigger.reset();
            collision_observed = false;
            elevation_observed = false;
            observations.reset();
            dynamic_prop_moved = false;
            simulated_ticks = 0;
            clock.reset();
        };
        if (input.reset.pressed) {
            scene->reset();
            world_camera = scene->initial_camera();
            forget_observations();
        }
        if (input.toggle_debug_visuals.pressed) {
            debug_visuals.toggle();
            log(LogLevel::info, debug_visuals.enabled() ? "Debug visuals enabled."
                                                        : "Debug visuals disabled.");
        }
        // Escape cancels editing inside the shell; it must not quit the game.
        SetExitKey(editor && editor->visible() ? KEY_NULL : KEY_ESCAPE);
        if (input.toggle_editor.pressed && editor_ready()) {
            editor->toggle_visible();
            log(LogLevel::info, editor->visible() ? "Development editor shown."
                                                  : "Development editor hidden.");
        }
        if (input.cycle_render_pacing.pressed) {
            config.render_pacing = next_render_pacing(config.render_pacing);
            apply_render_pacing(config.render_pacing);
            log(LogLevel::info, "Render pacing changed to " +
                                    render_pacing_description(config.render_pacing) + ".");
        }
        if (input.toggle_gpu_background.pressed) {
            gpu_background_enabled = !gpu_background_enabled;
        }
#endif

        TickPlan tick_plan{};
        if (paused) {
            clock.reset();
            if (input.step_simulation.pressed) {
                tick_plan.fixed_steps = 1;
            }
        } else {
            tick_plan = clock.advance(frame_seconds);
        }

        for (std::uint32_t step = 0;
             step < tick_plan.fixed_steps &&
             (config.max_fixed_ticks == 0 || simulated_ticks < config.max_fixed_ticks);
             ++step) {
            constexpr float automated_axis = 0.70710678F;
            const Vec2 camera_movement{
                config.automated_movement ? automated_axis : input.move_horizontal,
                config.automated_movement ? automated_axis : input.move_depth,
            };
            const Vec3 world_movement =
                camera_ground_direction_to_world(camera_movement, world_camera);
            const RuntimeSceneTickResult scene_tick = scene->tick(
                {world_movement.x, world_movement.z}, static_cast<float>(fixed_step_seconds));
            movement_blocked = scene_tick.player_blocked;
            active_trigger = scene_tick.active_trigger;
            collision_observed = collision_observed || scene_tick.player_blocked;
            elevation_observed = elevation_observed || scene_tick.player_elevated;
            dynamic_prop_moved = dynamic_prop_moved || scene_tick.primary_prop_moved;
            runtime_layers.dispatch(scene_tick.events);
            runtime_layers.fixed_update({
                .tick = simulated_ticks + 1,
                .seconds = static_cast<float>(fixed_step_seconds),
            });

            const Vec3 player_position = scene->player_position();
            const float follow_weight =
                std::min(1.0F, 8.0F * static_cast<float>(fixed_step_seconds));
            world_camera.focus.x +=
                (player_position.x - world_camera.focus.x) * follow_weight;
            world_camera.focus.z +=
                (player_position.z - world_camera.focus.z) * follow_weight;
            ++simulated_ticks;
        }

        const float interpolation_alpha = static_cast<float>(tick_plan.interpolation_alpha);
        const Vec3 current_player_position = scene->player_position();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const std::string pacing_description = render_pacing_description(config.render_pacing);
#endif
        const std::vector<RenderItem2D> render_items =
            scene->collect_render_items(interpolation_alpha);

        // Re-read the ground definition every frame because the editor can
        // replace the running scene between frames.
        const GroundMapDefinition& ground_definition = scene->ground_definition();
        render_queue.begin(render_camera);
        render_queue.submit_ground(project_ground_quad(
            1, ground_definition.walkable_bounds, 0.0F, {22, 61, 57, 218}, world_camera));
        std::uint64_t ground_id = 2;
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        float maximum_authored_elevation = 0.0F;
        for (const GroundArea& area : ground_definition.areas) {
            maximum_authored_elevation = std::max(maximum_authored_elevation, area.elevation);
        }
#endif
        for (const GroundArea& area : ground_definition.areas) {
            if (area.kind == GroundAreaKind::elevation) {
                render_queue.submit_ground(project_ground_quad(
                    ground_id++, area.bounds, area.elevation, {48, 103, 78, 238}, world_camera));
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
                if (debug_visuals.draws(DebugChannel::elevation_map)) {
                    render_queue.submit_ground(project_ground_quad(
                        ground_id++, area.bounds, area.elevation + 0.12F,
                        debug_elevation_tint(area.elevation, maximum_authored_elevation),
                        world_camera));
                }
#endif
                continue;
            }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
            const bool solid_area = area.kind == GroundAreaKind::solid;
            if (!debug_visuals.draws(solid_area ? DebugChannel::collision_shapes
                                                : DebugChannel::trigger_volumes)) {
                continue;
            }
            const ColorRgba8 debug_tint = solid_area ? ColorRgba8{128, 58, 49, 155}
                                                     : ColorRgba8{49, 164, 184, 155};
            render_queue.submit_ground(project_ground_quad(
                ground_id++, area.bounds, 0.15F, debug_tint, world_camera));
#endif
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        for (const PhysicsFootprint& footprint : scene->debug_footprints()) {
            if (!debug_visuals.draws(footprint.sensor ? DebugChannel::trigger_volumes
                                                      : DebugChannel::collision_shapes)) {
                continue;
            }
            const ColorRgba8 tint = footprint.sensor
                                        ? ColorRgba8{55, 204, 222, 80}
                                        : footprint.motion == PhysicsMotionType::dynamic_body
                                              ? ColorRgba8{248, 194, 89, 115}
                                              : footprint.motion == PhysicsMotionType::kinematic_body
                                                    ? ColorRgba8{74, 222, 190, 95}
                                                    : ColorRgba8{170, 112, 220, 70};
            render_queue.submit_ground(project_physics_footprint(footprint, tint, world_camera));
        }
#endif
        for (const RenderItem2D& item : render_items) {
            const ProjectedPoint25D projected = project_world_point(item.transform.position, world_camera);
            render_queue.submit(SpriteSubmission2D{
                .stable_id = item.entity.value,
                .texture = item.sprite.texture,
                .source = item.sprite.source,
                .position = projected.position,
                .size = item.sprite.size,
                .scale = {item.transform.scale.x, item.transform.scale.y},
                .normalized_origin = item.sprite.normalized_origin,
                .rotation_degrees = item.sprite.rotation_degrees,
                .sort_depth = projected.depth,
                .tint = item.sprite.tint,
                .layer = item.sprite.layer,
            });
        }
        const RenderFrame2D render_frame = render_queue.finish();

        BeginTextureMode(canvas);
        const bool gpu_backdrop_active = draw_background(config, gpu_background_enabled, gpu_backdrop);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (debug_visuals.draws(DebugChannel::world_grid)) {
            draw_projected_ground_grid(config, world_camera, ground_definition.walkable_bounds);
        }
#endif
        const RenderDiagnostics2D render_diagnostics =
            renderer.render(render_frame, config.canvas_width, config.canvas_height);
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (debug_visuals.draws(DebugChannel::stats_overlay)) {
            draw_overlay(config, paused, tick_plan.dropped_time, simulated_ticks,
                         pacing_description, gpu_backdrop_active, jobs.worker_count(),
                         textures.loaded_texture_count(), scene_schema_version, stable_uuid_count,
                         current_player_position, movement_blocked, active_trigger,
                         observations.physics_contact_observed, dynamic_prop_moved, world_camera,
                         render_diagnostics, debug_visuals, editor && editor->visible());
        }
#else
        static_cast<void>(gpu_backdrop_active);
        static_cast<void>(render_diagnostics);
#endif
        EndTextureMode();

        const CanvasViewport viewport = compute_canvas_viewport(
            GetScreenWidth(), GetScreenHeight(), config.canvas_width, config.canvas_height);
        const Rectangle source{
            0.0F,
            0.0F,
            static_cast<float>(config.canvas_width),
            -static_cast<float>(config.canvas_height),
        };
        const Rectangle destination{viewport.x, viewport.y, viewport.width, viewport.height};

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        const bool editor_visible = editor && editor->visible() && scene_editor;
        EditorActions editor_actions{};
#else
        constexpr bool editor_visible = false;
#endif

        BeginDrawing();
        ClearBackground(BLACK);
        if (!editor_visible) {
            DrawTexturePro(canvas.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        }
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (editor_visible) {
            editor_actions = editor->draw(
                *scene_editor, debug_visuals,
                EditorStats{
                    .frames_per_second = GetFPS(),
                    .fixed_update_hz = config.fixed_update_hz,
                    .simulated_ticks = simulated_ticks,
                    .entity_count = scene->entity_count(),
                    .physics_body_count = scene->physics_body_count(),
                    .loaded_texture_count = textures.loaded_texture_count(),
                    .visible_sprites = render_diagnostics.visible_sprites,
                    .culled_sprites = render_diagnostics.culled_sprites,
                    .estimated_batches = render_diagnostics.estimated_batches,
                    .paused = paused,
                },
                EditorCanvas{
                    .texture_id = canvas.texture.id,
                    .width = config.canvas_width,
                    .height = config.canvas_height,
                });
        }
#endif
        EndDrawing();
        ++rendered_frames;

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
        if (editor_actions.toggle_pause) {
            paused = !paused;
            clock.reset();
        }
        if (editor_actions.reset_running_scene) {
            scene->reset();
            world_camera = scene->initial_camera();
            forget_observations();
        }
        if (editor_actions.apply_document_to_running_scene && scene_editor) {
            try {
                // Play mode consumes a validated copy of the edited document;
                // the authored file is untouched until the editor saves it.
                auto edited = std::make_unique<RuntimeScene>(scene_editor->runtime_copy(), textures);
                scene = std::move(edited);
                world_camera = scene->initial_camera();
                stable_uuid_count = scene->world_snapshot().entities.size();
                forget_observations();
                log(LogLevel::info,
                    "Applied the edited scene document to the running scene: " +
                        std::to_string(scene->entity_count()) + " entities.");
            } catch (const std::exception& error) {
                log(LogLevel::error,
                    std::string{"Applying the edited scene document failed: "} + error.what());
            }
        }
#endif

        const bool reached_capture_frame = config.capture_frame > 0 && rendered_frames >= config.capture_frame;
        const bool reached_capture_tick = config.capture_tick > 0 && simulated_ticks >= config.capture_tick;
        if (!captured_smoke_frame && !config.capture_path.empty() &&
            (reached_capture_frame || reached_capture_tick)) {
            TakeScreenshot(config.capture_path.c_str());
            if (std::filesystem::is_regular_file(config.capture_path)) {
                log(LogLevel::info, "Captured runtime smoke-test frame.");
            } else {
                log(LogLevel::error, "Runtime smoke-test frame could not be written.");
                smoke_capture_failed = true;
            }
            captured_smoke_frame = true;
        }
    }

    const Vec3 final_player_position = scene->player_position();
    const Vec3 final_dynamic_prop_position = scene->primary_prop_position();
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS
    // Release the editor context and font atlas while the window still exists.
    editor.reset();
    scene_editor.reset();
#endif
    gpu_backdrop.release();
    scene.reset();
    const bool texture_lifetime_valid = textures.loaded_texture_count() == 0;
    textures.shutdown();
    UnloadRenderTexture(canvas);
    CloseWindow();
    log(LogLevel::info, "Completed " + std::to_string(simulated_ticks) + " fixed simulation ticks.");
    log(LogLevel::info, "Final camera X/Z: " + std::to_string(world_camera.focus.x) + "/" +
                            std::to_string(world_camera.focus.z) + ".");
    if (config.automated_movement) {
        log(LogLevel::info,
            "Replay state hash: " +
                std::to_string(replay_state_hash(
                    final_player_position, final_dynamic_prop_position, simulated_ticks,
                    collision_observed, elevation_observed, observations.trigger_observed,
                    observations.physics_contact_observed)) +
                ".");
    }
    if (config.automated_movement &&
        (!collision_observed || !elevation_observed)) {
        log(LogLevel::error, "Automated ground-map validation missed collision or elevation state.");
        return 7;
    }
    if (config.automated_movement) {
        log(LogLevel::info, "Automated ground collision and elevation validation passed.");
    }
    if (config.automated_movement &&
        (!observations.physics_contact_observed || !observations.trigger_observed ||
         !dynamic_prop_moved)) {
        log(LogLevel::error,
            "Automated Physics2D validation missed a contact, trigger, or dynamic prop movement.");
        return 9;
    }
    if (config.automated_movement) {
        log(LogLevel::info,
            "Automated Physics2D contact, trigger, and dynamic prop validation passed.");
    }
    if (config.automated_movement &&
        (!observations.animation_event_observed ||
         !observations.animation_identity_observed)) {
        log(LogLevel::error,
            "Automated animation validation observed no stable-identity frame event.");
        return 10;
    }
    if (config.automated_movement && !observations.diagonal_animation_observed) {
        log(LogLevel::error, "Automated locomotion validation observed no diagonal player clip.");
        return 11;
    }
    if (config.automated_movement) {
        log(LogLevel::info, "Automated locomotion animation and frame-event validation passed.");
    }
    if (!texture_lifetime_valid) {
        log(LogLevel::error, "Texture lifetime validation failed during shutdown.");
        return 6;
    }
    if (smoke_capture_failed) {
        return 8;
    }
    log(LogLevel::info, "Texture lifetime validation passed.");
    log(LogLevel::info, "Application shut down cleanly.");
    return 0;
}

} // namespace ic2d
