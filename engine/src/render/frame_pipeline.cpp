#include "render/frame_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <raylib.h>

namespace ic2d {
namespace {

[[nodiscard]] float finite_clamp(
    const float value,
    const float fallback,
    const float minimum,
    const float maximum
) noexcept {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

[[nodiscard]] PostProcessSettings2D sanitized(PostProcessSettings2D settings) noexcept {
    settings.exposure = finite_clamp(settings.exposure, 1.0F, 0.0F, 4.0F);
    settings.saturation = finite_clamp(settings.saturation, 1.0F, 0.0F, 2.0F);
    settings.vignette_strength =
        finite_clamp(settings.vignette_strength, 0.16F, 0.0F, 1.0F);
    return settings;
}

[[nodiscard]] bool valid_target(const RenderTexture2D& target) noexcept {
    return target.id != 0U && target.texture.id != 0U;
}

} // namespace

struct FramePipeline2D::Impl {
    int width{0};
    int height{0};
    RenderTexture2D scene_target{};
    RenderTexture2D post_process_target{};
    Shader post_process_shader{};
    int exposure_location{-1};
    int saturation_location{-1};
    int vignette_location{-1};
    bool scene_active{false};
    bool post_process_output{false};
    FramePipelineDiagnostics2D diagnostics{};
};

FramePipeline2D::FramePipeline2D(
    const int width,
    const int height,
    const std::filesystem::path& post_process_fragment_shader
) : impl_{std::make_unique<Impl>()} {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument{"Frame pipeline dimensions must be greater than zero."};
    }

    impl_->width = width;
    impl_->height = height;
    impl_->scene_target = LoadRenderTexture(width, height);
    impl_->post_process_target = LoadRenderTexture(width, height);
    if (valid_target(impl_->scene_target)) {
        SetTextureFilter(impl_->scene_target.texture, TEXTURE_FILTER_POINT);
    }
    if (valid_target(impl_->post_process_target)) {
        SetTextureFilter(impl_->post_process_target.texture, TEXTURE_FILTER_POINT);
    }

    const std::string shader_path = post_process_fragment_shader.string();
    impl_->post_process_shader = LoadShader(nullptr, shader_path.c_str());
    if (IsShaderValid(impl_->post_process_shader)) {
        impl_->exposure_location = GetShaderLocation(impl_->post_process_shader, "exposure");
        impl_->saturation_location = GetShaderLocation(impl_->post_process_shader, "saturation");
        impl_->vignette_location = GetShaderLocation(impl_->post_process_shader, "vignetteStrength");
    }
    impl_->diagnostics.post_process_available = post_process_available();
}

FramePipeline2D::~FramePipeline2D() {
    release();
}

bool FramePipeline2D::available() const noexcept {
    return impl_ && valid_target(impl_->scene_target);
}

bool FramePipeline2D::post_process_available() const noexcept {
    return available() && valid_target(impl_->post_process_target) &&
           IsShaderValid(impl_->post_process_shader) && impl_->exposure_location >= 0 &&
           impl_->saturation_location >= 0 && impl_->vignette_location >= 0;
}

std::uint32_t FramePipeline2D::output_texture_id() const noexcept {
    if (!available()) {
        return 0U;
    }
    return impl_->post_process_output ? impl_->post_process_target.texture.id
                                      : impl_->scene_target.texture.id;
}

int FramePipeline2D::width() const noexcept {
    return impl_ ? impl_->width : 0;
}

int FramePipeline2D::height() const noexcept {
    return impl_ ? impl_->height : 0;
}

const FramePipelineDiagnostics2D& FramePipeline2D::diagnostics() const noexcept {
    static const FramePipelineDiagnostics2D empty{};
    return impl_ ? impl_->diagnostics : empty;
}

void FramePipeline2D::begin_scene() {
    if (!available()) {
        throw std::logic_error{"Cannot begin an unavailable frame pipeline."};
    }
    if (impl_->scene_active) {
        throw std::logic_error{"Frame pipeline scene pass is already active."};
    }
    impl_->post_process_output = false;
    impl_->scene_active = true;
    BeginTextureMode(impl_->scene_target);
}

void FramePipeline2D::finish_scene(const PostProcessSettings2D& requested_settings) {
    if (!impl_ || !impl_->scene_active) {
        throw std::logic_error{"Frame pipeline finish requires an active scene pass."};
    }
    EndTextureMode();
    impl_->scene_active = false;

    const PostProcessSettings2D settings = sanitized(requested_settings);
    impl_->diagnostics = {
        .post_process_requested = settings.enabled,
        .post_process_available = post_process_available(),
        .post_process_active = false,
        .estimated_gpu_passes = 2U,
        .render_target_switches = 2U,
        .shader_passes = 0U,
    };
    if (!settings.enabled || !post_process_available()) {
        return;
    }

    SetShaderValue(impl_->post_process_shader, impl_->exposure_location,
                   &settings.exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->post_process_shader, impl_->saturation_location,
                   &settings.saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(impl_->post_process_shader, impl_->vignette_location,
                   &settings.vignette_strength, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(impl_->post_process_target);
    ClearBackground(BLANK);
    BeginShaderMode(impl_->post_process_shader);
    const Rectangle source{
        0.0F,
        0.0F,
        static_cast<float>(impl_->width),
        -static_cast<float>(impl_->height),
    };
    const Rectangle destination{
        0.0F,
        0.0F,
        static_cast<float>(impl_->width),
        static_cast<float>(impl_->height),
    };
    DrawTexturePro(impl_->scene_target.texture, source, destination,
                   Vector2{0.0F, 0.0F}, 0.0F, WHITE);
    EndShaderMode();
    EndTextureMode();

    impl_->post_process_output = true;
    impl_->diagnostics.post_process_active = true;
    impl_->diagnostics.estimated_gpu_passes = 3U;
    impl_->diagnostics.render_target_switches = 3U;
    impl_->diagnostics.shader_passes = 1U;
}

void FramePipeline2D::present(const RectF& destination) const {
    if (!available() || destination.width <= 0.0F || destination.height <= 0.0F) {
        return;
    }
    const Texture2D texture = impl_->post_process_output
                                  ? impl_->post_process_target.texture
                                  : impl_->scene_target.texture;
    const Rectangle source{
        0.0F,
        0.0F,
        static_cast<float>(impl_->width),
        -static_cast<float>(impl_->height),
    };
    const Rectangle target{destination.x, destination.y, destination.width, destination.height};
    DrawTexturePro(texture, source, target, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
}

void FramePipeline2D::release() noexcept {
    if (!impl_) {
        return;
    }
    if (impl_->scene_active) {
        EndTextureMode();
        impl_->scene_active = false;
    }
    if (IsShaderValid(impl_->post_process_shader)) {
        UnloadShader(impl_->post_process_shader);
        impl_->post_process_shader = {};
    }
    if (valid_target(impl_->post_process_target)) {
        UnloadRenderTexture(impl_->post_process_target);
        impl_->post_process_target = {};
    }
    if (valid_target(impl_->scene_target)) {
        UnloadRenderTexture(impl_->scene_target);
        impl_->scene_target = {};
    }
    impl_->post_process_output = false;
    impl_->diagnostics = {};
}

} // namespace ic2d
