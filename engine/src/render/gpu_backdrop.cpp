#include "render/gpu_backdrop.hpp"

#include <raylib.h>

namespace ic2d {
namespace {

constexpr const char* backdrop_fragment_shader = R"glsl(
#version 330

uniform vec2 resolution;
out vec4 finalColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec3 low = vec3(11.0 / 255.0, 18.0 / 255.0, 29.0 / 255.0);
    vec3 high = vec3(22.0 / 255.0, 34.0 / 255.0, 48.0 / 255.0);
    vec3 background = mix(low, high, uv.y);
    vec2 centred = uv * 2.0 - 1.0;
    float vignette = smoothstep(0.35, 1.35, dot(centred, centred));
    finalColor = vec4(background * mix(1.0, 0.72, vignette), 1.0);
}
)glsl";

} // namespace

struct GpuBackdrop::Impl {
    Shader shader{};
    int resolution_location{-1};
};

GpuBackdrop::GpuBackdrop() : impl_{std::make_unique<Impl>()} {
    impl_->shader = LoadShaderFromMemory(nullptr, backdrop_fragment_shader);
    if (IsShaderValid(impl_->shader)) {
        impl_->resolution_location = GetShaderLocation(impl_->shader, "resolution");
    }
}

GpuBackdrop::~GpuBackdrop() { release(); }

bool GpuBackdrop::available() const noexcept {
    return impl_ && IsShaderValid(impl_->shader) && impl_->resolution_location >= 0;
}

void GpuBackdrop::draw(const int width, const int height) const {
    if (!available()) {
        return;
    }

    const float resolution[]{static_cast<float>(width), static_cast<float>(height)};
    SetShaderValue(impl_->shader, impl_->resolution_location, resolution, SHADER_UNIFORM_VEC2);
    BeginShaderMode(impl_->shader);
    DrawRectangle(0, 0, width, height, WHITE);
    EndShaderMode();
}

void GpuBackdrop::release() noexcept {
    if (impl_ && IsShaderValid(impl_->shader)) {
        UnloadShader(impl_->shader);
        impl_->shader = {};
        impl_->resolution_location = -1;
    }
}

} // namespace ic2d
