#pragma once

#include "ic2d/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ic2d {

enum class AnimationLoopMode {
    once,
    loop,
    ping_pong,
};

enum class AnimationTransitionMode {
    restart,
    preserve_cycle_phase,
};

struct AnimationFrame {
    RectF source{};
    std::uint32_t duration_ticks{1};
    std::vector<std::string> events;
    bool flip_x{false};
    // Presentation-only scaling lets authored effect frames expand beyond an
    // actor's normal sprite box without changing its transform or collision.
    float presentation_scale{1.0F};
};

struct AnimationClip {
    std::string id;
    AnimationLoopMode loop_mode{AnimationLoopMode::loop};
    std::vector<AnimationFrame> frames;
};

struct AnimationFrameEvent {
    std::string clip_id;
    std::string name;
    std::size_t frame_index{0};
};

struct AnimationSample {
    // Valid for the lifetime of the player; clip storage is immutable.
    std::string_view clip_id;
    RectF source{};
    std::size_t frame_index{0};
    bool paused{false};
    bool finished{false};
    bool flip_x{false};
    float presentation_scale{1.0F};
};

// Deterministic integer-tick clip playback. Construction validates the complete
// clip set. Frame events are emitted when advance() enters an authored frame.
class AnimationPlayer final {
public:
    AnimationPlayer(std::vector<AnimationClip> clips, std::string initial_clip);
    ~AnimationPlayer();

    AnimationPlayer(const AnimationPlayer&) = delete;
    AnimationPlayer& operator=(const AnimationPlayer&) = delete;
    AnimationPlayer(AnimationPlayer&&) noexcept;
    AnimationPlayer& operator=(AnimationPlayer&&) noexcept;

    // Returns true when playback state changed. Unknown clip ids throw.
    [[nodiscard]] bool play(std::string_view clip_id, bool restart = false);
    // Phase-preserving transitions keep locomotion gait aligned when the
    // selected directional clip changes. Once clips fall back to a restart.
    [[nodiscard]] bool play(std::string_view clip_id, AnimationTransitionMode transition);
    void set_paused(bool paused) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::vector<AnimationFrameEvent> advance(std::uint32_t ticks = 1);
    [[nodiscard]] AnimationSample sample() const;
    [[nodiscard]] std::size_t clip_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
