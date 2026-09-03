#pragma once

#include "ic2d/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ic2d {

struct PostProcessSettings2D {
    bool enabled{true};
    float exposure{1.0F};
    float saturation{1.0F};
    float vignette_strength{0.16F};
};

struct FramePipelineDiagnostics2D {
    bool post_process_requested{false};
    bool post_process_available{false};
    bool post_process_active{false};
    std::uint32_t estimated_gpu_passes{0};
    std::uint32_t render_target_switches{0};
    std::uint32_t shader_passes{0};
};

// Owns the complete off-screen render and presentation chain. Raylib targets,
// shaders, uniform locations, coordinate flips, and release order stay private.
class FramePipeline2D final {
public:
    FramePipeline2D(int width, int height,
                    const std::filesystem::path& post_process_fragment_shader);
    ~FramePipeline2D();

    FramePipeline2D(const FramePipeline2D&) = delete;
    FramePipeline2D& operator=(const FramePipeline2D&) = delete;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool post_process_available() const noexcept;
    [[nodiscard]] std::uint32_t output_texture_id() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] const FramePipelineDiagnostics2D& diagnostics() const noexcept;

    void begin_scene();
    void finish_scene(const PostProcessSettings2D& settings);
    void present(const RectF& destination) const;
    void release() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
