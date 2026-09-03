#include "ic2d/animation.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ic2d {
namespace {

[[nodiscard]] bool valid_source(const RectF& source) noexcept {
    return std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.width) &&
           std::isfinite(source.height) && source.width > 0.0F && source.height > 0.0F;
}

} // namespace

struct AnimationPlayer::Impl {
    Impl(std::vector<AnimationClip> authored_clips, std::string authored_initial_clip)
        : clips{std::move(authored_clips)}, initial_clip{std::move(authored_initial_clip)} {
        if (clips.empty()) {
            throw std::invalid_argument{"Animation players require at least one clip."};
        }
        for (std::size_t clip_index = 0; clip_index < clips.size(); ++clip_index) {
            const AnimationClip& clip = clips[clip_index];
            if (clip.id.empty() || !clip_indices.emplace(clip.id, clip_index).second) {
                throw std::invalid_argument{"Animation clip ids must be non-empty and unique."};
            }
            if (clip.frames.empty()) {
                throw std::invalid_argument{"Animation clips require at least one frame."};
            }
            for (const AnimationFrame& frame : clip.frames) {
                if (!valid_source(frame.source) || frame.duration_ticks == 0 ||
                    !std::isfinite(frame.presentation_scale) ||
                    frame.presentation_scale <= 0.0F) {
                    throw std::invalid_argument{
                        "Animation frames require a finite positive source, duration, and "
                        "presentation scale."};
                }
                std::unordered_set<std::string> event_names;
                for (const std::string& event : frame.events) {
                    if (event.empty() || !event_names.insert(event).second) {
                        throw std::invalid_argument{
                            "Animation frame events must be non-empty and unique per frame."};
                    }
                }
            }
        }
        const auto initial = clip_indices.find(initial_clip);
        if (initial == clip_indices.end()) {
            throw std::invalid_argument{"The initial animation clip is not in the clip set."};
        }
        current_clip = initial->second;
    }

    void enter_frame(std::vector<AnimationFrameEvent>& output) const {
        const AnimationClip& clip = clips[current_clip];
        for (const std::string& event : clip.frames[current_frame].events) {
            output.push_back({
                .clip_id = clip.id,
                .name = event,
                .frame_index = current_frame,
            });
        }
    }

    void transition(std::vector<AnimationFrameEvent>& output) {
        const AnimationClip& clip = clips[current_clip];
        const std::size_t frame_count = clip.frames.size();
        if (clip.loop_mode == AnimationLoopMode::once) {
            if (current_frame + 1 >= frame_count) {
                finished = true;
                return;
            }
            ++current_frame;
        } else if (clip.loop_mode == AnimationLoopMode::loop) {
            current_frame = (current_frame + 1) % frame_count;
        } else if (frame_count == 1) {
            current_frame = 0;
        } else if (direction > 0) {
            if (current_frame + 1 < frame_count) {
                ++current_frame;
            } else {
                direction = -1;
                --current_frame;
            }
        } else if (current_frame > 0) {
            --current_frame;
        } else {
            direction = 1;
            ++current_frame;
        }
        enter_frame(output);
    }

    void rewind(const std::size_t clip_index) noexcept {
        current_clip = clip_index;
        current_frame = 0;
        ticks_in_frame = 0;
        direction = 1;
        finished = false;
    }

    [[nodiscard]] std::uint64_t cycle_duration(const AnimationClip& clip) const noexcept {
        std::uint64_t duration = 0;
        for (const AnimationFrame& frame : clip.frames) {
            duration += frame.duration_ticks;
        }
        return duration;
    }

    void preserve_cycle_phase(const std::size_t clip_index) noexcept {
        const AnimationClip& source = clips[current_clip];
        const AnimationClip& target = clips[clip_index];
        if (source.loop_mode == AnimationLoopMode::once ||
            target.loop_mode == AnimationLoopMode::once) {
            rewind(clip_index);
            return;
        }

        std::uint64_t source_tick = ticks_in_frame;
        for (std::size_t frame = 0; frame < current_frame; ++frame) {
            source_tick += source.frames[frame].duration_ticks;
        }
        const std::uint64_t source_duration = cycle_duration(source);
        const std::uint64_t target_duration = cycle_duration(target);
        std::uint64_t target_tick = source_tick * target_duration / source_duration;
        if (target_tick >= target_duration) {
            target_tick = target_duration - 1;
        }

        current_clip = clip_index;
        current_frame = 0;
        while (target_tick >= target.frames[current_frame].duration_ticks) {
            target_tick -= target.frames[current_frame].duration_ticks;
            ++current_frame;
        }
        ticks_in_frame = static_cast<std::uint32_t>(target_tick);
        direction = 1;
        finished = false;
    }

    std::vector<AnimationClip> clips;
    std::unordered_map<std::string, std::size_t> clip_indices;
    std::string initial_clip;
    std::size_t current_clip{0};
    std::size_t current_frame{0};
    std::uint32_t ticks_in_frame{0};
    int direction{1};
    bool paused{false};
    bool finished{false};
};

AnimationPlayer::AnimationPlayer(std::vector<AnimationClip> clips, std::string initial_clip)
    : impl_{std::make_unique<Impl>(std::move(clips), std::move(initial_clip))} {}

AnimationPlayer::~AnimationPlayer() = default;
AnimationPlayer::AnimationPlayer(AnimationPlayer&&) noexcept = default;
AnimationPlayer& AnimationPlayer::operator=(AnimationPlayer&&) noexcept = default;

bool AnimationPlayer::play(const std::string_view clip_id, const bool restart) {
    const auto found = impl_->clip_indices.find(std::string{clip_id});
    if (found == impl_->clip_indices.end()) {
        throw std::out_of_range{"Unknown animation clip: " + std::string{clip_id}};
    }
    if (found->second == impl_->current_clip && !restart) {
        return false;
    }
    impl_->rewind(found->second);
    return true;
}

bool AnimationPlayer::play(const std::string_view clip_id,
                           const AnimationTransitionMode transition) {
    const auto found = impl_->clip_indices.find(std::string{clip_id});
    if (found == impl_->clip_indices.end()) {
        throw std::out_of_range{"Unknown animation clip: " + std::string{clip_id}};
    }
    if (found->second == impl_->current_clip) {
        return false;
    }
    if (transition == AnimationTransitionMode::preserve_cycle_phase) {
        impl_->preserve_cycle_phase(found->second);
    } else {
        impl_->rewind(found->second);
    }
    return true;
}

void AnimationPlayer::set_paused(const bool paused) noexcept { impl_->paused = paused; }

void AnimationPlayer::reset() noexcept {
    const auto initial = impl_->clip_indices.find(impl_->initial_clip);
    impl_->rewind(initial->second);
    impl_->paused = false;
}

std::vector<AnimationFrameEvent> AnimationPlayer::advance(std::uint32_t ticks) {
    std::vector<AnimationFrameEvent> events;
    if (impl_->paused || impl_->finished || ticks == 0) {
        return events;
    }

    while (ticks > 0 && !impl_->finished) {
        const AnimationFrame& frame =
            impl_->clips[impl_->current_clip].frames[impl_->current_frame];
        const std::uint32_t ticks_to_boundary = frame.duration_ticks - impl_->ticks_in_frame;
        if (ticks < ticks_to_boundary) {
            impl_->ticks_in_frame += ticks;
            break;
        }
        ticks -= ticks_to_boundary;
        impl_->ticks_in_frame = 0;
        impl_->transition(events);
    }
    return events;
}

AnimationSample AnimationPlayer::sample() const {
    const AnimationClip& clip = impl_->clips[impl_->current_clip];
    const AnimationFrame& frame = clip.frames[impl_->current_frame];
    return {
        .clip_id = clip.id,
        .source = frame.source,
        .frame_index = impl_->current_frame,
        .paused = impl_->paused,
        .finished = impl_->finished,
        .flip_x = frame.flip_x,
        .presentation_scale = frame.presentation_scale,
    };
}

std::size_t AnimationPlayer::clip_count() const noexcept { return impl_->clips.size(); }

} // namespace ic2d
