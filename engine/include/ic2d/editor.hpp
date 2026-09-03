#pragma once

#include "ic2d/actor_debug.hpp"
#include "ic2d/aiming.hpp"
#include "ic2d/combat.hpp"
#include "ic2d/debug_visuals.hpp"
#include "ic2d/enemy_intent.hpp"
#include "ic2d/gameplay_state.hpp"
#include "ic2d/health.hpp"
#include "ic2d/identity.hpp"
#include "ic2d/input.hpp"
#include "ic2d/interaction.hpp"
#include "ic2d/nav_agent.hpp"
#include "ic2d/nav_grid.hpp"
#include "ic2d/nav_pathfinding.hpp"
#include "ic2d/projectiles.hpp"
#include "ic2d/scene_editor.hpp"
#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ic2d {

// Backend-independent editor focus policy, kept public so the exact input
// routing regression can be exercised without creating a GPU window.
[[nodiscard]] bool editor_blocks_gameplay_input(bool wants_text_input, bool item_active,
                                                bool gizmo_active) noexcept;

// Whether the scene in front of an author is being edited or played.
//
// Editing is not a pause. A paused run is a run that has already happened and
// is waiting to continue; editing is the authored scene, untouched, exactly as
// the document describes it. An editor that opened onto a paused run would be
// showing a state no document records, which is why the two are named apart
// rather than sharing one flag.
enum class EditorRunState : std::uint8_t {
    editing,
    running,
    paused,
};

// True when fixed ticks are advancing.
[[nodiscard]] constexpr bool simulating(const EditorRunState state) noexcept {
    return state == EditorRunState::running;
}

// What a pointer held on the editor's own window chrome is doing to it. The
// shell hit-tests and names the gesture; the application owns the window and is
// the only thing that may move or size it.
enum class EditorWindowDrag : std::uint8_t {
    none,
    move,
    resize_left,
    resize_right,
    resize_top,
    resize_bottom,
    resize_top_left,
    resize_top_right,
    resize_bottom_left,
    resize_bottom_right,
};

struct EditorWindowActions {
    bool minimize{false};
    bool toggle_maximize{false};
    bool close{false};
    // What is being held right now, and whether this frame is the one the hold
    // began on. The application anchors the window rect and the pointer once at
    // the start and resolves every later frame against that anchor, so a window
    // that moves under the pointer cannot chase itself across the desktop.
    EditorWindowDrag drag{EditorWindowDrag::none};
    bool drag_started{false};
};

// Read-only runtime figures the shell reports. The editor never reaches into
// the running scene for them, so it cannot disturb simulation state.
struct EditorStats {
    int frames_per_second{0};
    double fixed_update_hz{60.0};
    std::uint64_t simulated_ticks{0};
    std::size_t entity_count{0};
    std::size_t physics_body_count{0};
    std::size_t cpu_worker_count{0};
    std::size_t loaded_texture_count{0};
    std::size_t visible_sprites{0};
    std::size_t culled_sprites{0};
    std::size_t estimated_batches{0};
    std::size_t estimated_draw_calls{0};
    std::size_t visible_vertices{0};
    double frame_time_p50_ms{0.0};
    double frame_time_p95_ms{0.0};
    double frame_time_p99_ms{0.0};
    std::uint32_t estimated_gpu_passes{0};
    std::uint32_t render_target_switches{0};
    std::uint32_t shader_passes{0};
    std::size_t watched_texture_count{0};
    std::size_t successful_texture_reloads{0};
    std::size_t failed_texture_reloads{0};
    std::optional<GameplayStateDigest> gameplay_digest;
    NavGridSnapshot navigation_grid{};
    NavPathResult navigation_path{};
    NavAgentSnapshot navigation_agents{};
    InputFrame input{};
    AimingSnapshot aim{};
    InteractionSnapshot interaction{};
    CombatSnapshot combat{};
    std::uint64_t observed_combat_intents{0};
    std::optional<CombatIntentEvent> last_combat_intent;
    std::optional<DodgeStartedEvent> last_dodge;
    float dodge_distance_travelled{0.0F};
    bool dodge_movement_blocked{false};
    std::uint64_t observed_projectiles{0};
    std::optional<ProjectileSpawnedEvent> last_projectile;
    ProjectileSimulationSnapshot projectiles{};
    std::optional<ProjectileExpiredEvent> last_expired_projectile;
    std::optional<ProjectileImpactEvent> last_projectile_impact;
    EnemyIntentSnapshot enemy_intent{};
    std::optional<EnemyAcquiredTargetEvent> last_enemy_acquisition;
    std::optional<EnemyAttackRequestedEvent> last_enemy_attack;
    float enemy_distance_travelled{0.0F};
    float enemy_damage_applied_to_player{0.0F};
    bool enemy_attack_damage_enabled{true};
    bool enemy_movement_blocked{false};
    std::uint64_t invulnerable_enemy_attacks_rejected{0};
    HealthSnapshot health{};
    std::optional<DamageAppliedEvent> last_damage;
    std::optional<ActorDiedEvent> last_death;
    bool post_process_active{false};
    bool post_process_available{false};
    bool texture_hot_reload_enabled{false};
    EditorRunState run_state{EditorRunState::editing};
    // True when the application made an undecorated window and expects the
    // shell to supply the title bar it removed. False leaves every chrome
    // control and hit zone out, so a decorated window is untouched.
    bool custom_window_chrome{false};
    bool window_maximized{false};
    // Which runtime actor is which. The document knows placements, not roles,
    // so the shell learns from the running scene whether the selection is the
    // player, an attacker, or scenery, and offers only what suits it.
    EntityUuid player_uuid{};
    ActorDebugSnapshot actor_debug{};
    // True while the editor view is looking somewhere other than the gameplay
    // camera, so the shell can offer the way back.
    bool camera_detached{false};

    // Where the selected placement sits on the canvas, and which way one world
    // unit along X and Z points from there. The viewport draws a gizmo from
    // these, so the panel never learns the projection and the handles stay
    // correct under camera yaw. Absent when nothing suitable is selected.
    std::optional<Vec2> selection_canvas_point;
    Vec2 selection_axis_x_canvas{1.0F, 0.0F};
    Vec2 selection_axis_z_canvas{0.0F, 1.0F};
    // False for physics-bound placements, whose X and Z the body owns.
    bool selection_movable{false};
    float grid_snap_step{8.0F};
    // Authored .scene files discovered beside the loaded one, so the Debug
    // menu can swap scenes without the shell knowing any content paths.
    std::vector<std::filesystem::path> selectable_scenes;
    std::filesystem::path loaded_scene;
};

// What the shell asks the application to do after a frame of panels. Anything
// the editor can do safely by itself, such as saving, it already did.
struct EditorActions {
    bool apply_document_to_running_scene{false};
    bool reset_running_scene{false};
    // Present means put the scene into this state. Play, Pause and the pause
    // key all name the state they want rather than asking for a toggle, so two
    // of them pressed in the same frame cannot cancel out.
    std::optional<EditorRunState> set_run_state;
    // Present means rebuild the unsaved running scene with this many total
    // Runner actors. Zero restores only the authored actors.
    std::optional<std::size_t> enemy_stress_target_count;
    // Development overrides an author toggled this frame, in click order. A
    // vector rather than one request because nothing stops an author from
    // hitting two checkboxes before the frame ends.
    struct ActorDebugRequest {
        EntityUuid actor{};
        ActorDebugFlag flag{ActorDebugFlag::frozen};
        bool enabled{false};
    };
    std::vector<ActorDebugRequest> actor_debug_requests;
    // Present means damage this actor to death. Killing is an event with a
    // consequence the health module owns, not a state the editor holds, so it
    // is a request rather than a flag.
    std::optional<EntityUuid> kill_actor;
    // Present means load this authored scene and restart the running copy.
    std::optional<std::filesystem::path> load_scene_path;
    // A Ctrl+left click inside the viewport, in canvas pixels. The application owns
    // the camera and the running scene, so it resolves the click and reports
    // the result back through select_entity(). The editor stays free of
    // projection and renderer knowledge.
    bool viewport_picked{false};
    Vec2 viewport_pick_canvas_point{};

    // A translate-gizmo drag, reported as the total canvas offset from where
    // the drag began. The application owns the camera, so it converts that into
    // a world offset, previews it, and commits one move command on release.
    struct GizmoDrag {
        Vec2 canvas_offset{};
        bool finished{false};
        bool snap{false};
    };
    std::optional<GizmoDrag> gizmo_drag;

    EditorWindowActions window;

    // Editor camera intents. They are expressed in canvas pixels and wheel
    // notches so the shell never needs the world camera, and the application
    // never needs panel geometry.
    Vec2 camera_pan_canvas{};
    float camera_zoom_notches{0.0F};
    bool camera_frame_selection{false};
    bool camera_follow_player{false};
};

// The identity of the canvas the viewport panel displays. The value is a
// backend texture name; only the development editor consumes it.
struct EditorCanvas {
    std::uint32_t texture_id{0};
    int width{0};
    int height{0};
};

// Development-only docked editor shell. Dear ImGui, the docking layout, and
// the raylib draw backend stay private to this module. Every scene mutation it
// performs goes through SceneEditor, so undo, validation, and atomic saving
// behave exactly as they do for any other caller.
class EditorShell final {
public:
    // Empty uses the per-user Local App Data workspace. Tests and portable
    // tooling may provide an isolated override.
    explicit EditorShell(std::filesystem::path layout_path = {});
    ~EditorShell();

    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;
    EditorShell(EditorShell&&) noexcept;
    EditorShell& operator=(EditorShell&&) noexcept;

    // False when the shell could not create its context or font atlas.
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    void set_visible(bool visible) noexcept;
    void toggle_visible() noexcept;

    // Reported from the previous drawn frame, which is when the shell last
    // observed the pointer and keyboard.
    //
    // The game keeps running behind the panels, so gameplay keys keep reaching
    // it exactly as they do with the shell hidden. They are withheld only while
    // a text field is collecting keyboard input; mouse ownership is resolved
    // separately against the actual game viewport.
    [[nodiscard]] bool blocks_gameplay_input() const noexcept;
    [[nodiscard]] bool wants_mouse() const noexcept;
    [[nodiscard]] std::optional<Vec2> viewport_pointer_canvas() const noexcept;

    // The placement the panels are inspecting. A zero UUID means no selection.
    [[nodiscard]] EntityUuid selection() const noexcept;
    void select_entity(EntityUuid uuid) noexcept;

    // Call once per frame inside an active drawing pass. Does nothing and
    // returns no actions while the shell is hidden or unavailable.
    [[nodiscard]] EditorActions draw(SceneEditor& scene_editor, DebugVisuals& debug_visuals,
                                     const EditorStats& stats, const EditorCanvas& canvas);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
