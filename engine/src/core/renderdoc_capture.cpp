#include "core/renderdoc_capture.hpp"

#if IC2DE_ENABLE_DEVELOPMENT_TOOLS && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "core/renderdoc_app.h"
#endif

namespace ic2d {

RenderDocCapture::RenderDocCapture() {
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS && defined(_WIN32)
    if (const HMODULE module = GetModuleHandleA("renderdoc.dll")) {
        const auto get_api =
            reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        void* api = nullptr;
        if (get_api != nullptr && get_api(eRENDERDOC_API_Version_1_1_2, &api) == 1) {
            api_ = api;
        }
    }
#endif
}

void RenderDocCapture::tick(double interval_seconds, double frame_seconds) {
#if IC2DE_ENABLE_DEVELOPMENT_TOOLS && defined(_WIN32)
    if (api_ == nullptr || interval_seconds <= 0.0) {
        seconds_since_capture_ = 0.0;
        return;
    }
    auto* const api = static_cast<RENDERDOC_API_1_7_0*>(api_);
    if (api->IsFrameCapturing() != 0U) {
        return;
    }
    seconds_since_capture_ += frame_seconds;
    if (seconds_since_capture_ >= interval_seconds) {
        seconds_since_capture_ = 0.0;
        api->TriggerCapture();
    }
#else
    static_cast<void>(interval_seconds);
    static_cast<void>(frame_seconds);
#endif
}

} // namespace ic2d
