#include "editor/imgui_raylib_backend.hpp"

#include "ic2d/core/log.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <raylib.h>
#include <rlgl.h>

namespace ic2d {
namespace {

struct KeyMapping {
    int raylib_key;
    ImGuiKey imgui_key;
};

// Enough of the keyboard for panel navigation and text fields. Gameplay keys
// are read by the game's own adapter and are deliberately absent.
constexpr std::array<KeyMapping, 64> key_mappings{{
    {KEY_APOSTROPHE, ImGuiKey_Apostrophe},   {KEY_COMMA, ImGuiKey_Comma},
    {KEY_MINUS, ImGuiKey_Minus},             {KEY_PERIOD, ImGuiKey_Period},
    {KEY_SLASH, ImGuiKey_Slash},             {KEY_ZERO, ImGuiKey_0},
    {KEY_ONE, ImGuiKey_1},                   {KEY_TWO, ImGuiKey_2},
    {KEY_THREE, ImGuiKey_3},                 {KEY_FOUR, ImGuiKey_4},
    {KEY_FIVE, ImGuiKey_5},                  {KEY_SIX, ImGuiKey_6},
    {KEY_SEVEN, ImGuiKey_7},                 {KEY_EIGHT, ImGuiKey_8},
    {KEY_NINE, ImGuiKey_9},                  {KEY_SEMICOLON, ImGuiKey_Semicolon},
    {KEY_EQUAL, ImGuiKey_Equal},             {KEY_A, ImGuiKey_A},
    {KEY_B, ImGuiKey_B},                     {KEY_C, ImGuiKey_C},
    {KEY_D, ImGuiKey_D},                     {KEY_E, ImGuiKey_E},
    {KEY_F, ImGuiKey_F},                     {KEY_G, ImGuiKey_G},
    {KEY_H, ImGuiKey_H},                     {KEY_I, ImGuiKey_I},
    {KEY_J, ImGuiKey_J},                     {KEY_K, ImGuiKey_K},
    {KEY_L, ImGuiKey_L},                     {KEY_M, ImGuiKey_M},
    {KEY_N, ImGuiKey_N},                     {KEY_O, ImGuiKey_O},
    {KEY_P, ImGuiKey_P},                     {KEY_Q, ImGuiKey_Q},
    {KEY_R, ImGuiKey_R},                     {KEY_S, ImGuiKey_S},
    {KEY_T, ImGuiKey_T},                     {KEY_U, ImGuiKey_U},
    {KEY_V, ImGuiKey_V},                     {KEY_W, ImGuiKey_W},
    {KEY_X, ImGuiKey_X},                     {KEY_Y, ImGuiKey_Y},
    {KEY_Z, ImGuiKey_Z},                     {KEY_SPACE, ImGuiKey_Space},
    {KEY_ESCAPE, ImGuiKey_Escape},           {KEY_ENTER, ImGuiKey_Enter},
    {KEY_TAB, ImGuiKey_Tab},                 {KEY_BACKSPACE, ImGuiKey_Backspace},
    {KEY_INSERT, ImGuiKey_Insert},           {KEY_DELETE, ImGuiKey_Delete},
    {KEY_RIGHT, ImGuiKey_RightArrow},        {KEY_LEFT, ImGuiKey_LeftArrow},
    {KEY_DOWN, ImGuiKey_DownArrow},          {KEY_UP, ImGuiKey_UpArrow},
    {KEY_PAGE_UP, ImGuiKey_PageUp},          {KEY_PAGE_DOWN, ImGuiKey_PageDown},
    {KEY_HOME, ImGuiKey_Home},               {KEY_END, ImGuiKey_End},
    {KEY_KP_ENTER, ImGuiKey_KeypadEnter},    {KEY_LEFT_BRACKET, ImGuiKey_LeftBracket},
    {KEY_BACKSLASH, ImGuiKey_Backslash},     {KEY_RIGHT_BRACKET, ImGuiKey_RightBracket},
    {KEY_GRAVE, ImGuiKey_GraveAccent},       {KEY_CAPS_LOCK, ImGuiKey_CapsLock},
}};

void apply_mouse_cursor() {
    const ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0) {
        return;
    }
    switch (ImGui::GetMouseCursor()) {
    case ImGuiMouseCursor_TextInput:
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
        break;
    case ImGuiMouseCursor_ResizeEW:
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        break;
    case ImGuiMouseCursor_ResizeNS:
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        break;
    case ImGuiMouseCursor_ResizeNWSE:
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NWSE);
        break;
    case ImGuiMouseCursor_ResizeNESW:
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NESW);
        break;
    case ImGuiMouseCursor_ResizeAll:
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        break;
    case ImGuiMouseCursor_NotAllowed:
        SetMouseCursor(MOUSE_CURSOR_NOT_ALLOWED);
        break;
    default:
        SetMouseCursor(MOUSE_CURSOR_ARROW);
        break;
    }
}

void submit_draw_data(const ImDrawData& draw_data) {
    const int framebuffer_height = GetScreenHeight();
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    for (int list_index = 0; list_index < draw_data.CmdListsCount; ++list_index) {
        const ImDrawList* list = draw_data.CmdLists[list_index];
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                command.UserCallback(list, &command);
                continue;
            }

            const float clip_left = command.ClipRect.x - draw_data.DisplayPos.x;
            const float clip_top = command.ClipRect.y - draw_data.DisplayPos.y;
            const float clip_right = command.ClipRect.z - draw_data.DisplayPos.x;
            const float clip_bottom = command.ClipRect.w - draw_data.DisplayPos.y;
            if (clip_right <= clip_left || clip_bottom <= clip_top) {
                continue;
            }

            rlEnableScissorTest();
            rlScissor(static_cast<int>(clip_left),
                      framebuffer_height - static_cast<int>(clip_bottom),
                      static_cast<int>(clip_right - clip_left),
                      static_cast<int>(clip_bottom - clip_top));

            rlSetTexture(static_cast<unsigned int>(command.GetTexID()));
            rlBegin(RL_TRIANGLES);
            for (unsigned int element = 0; element + 2 < command.ElemCount; element += 3) {
                for (unsigned int corner = 0; corner < 3; ++corner) {
                    const ImDrawIdx index =
                        list->IdxBuffer.Data[command.IdxOffset + element + corner];
                    const ImDrawVert& vertex =
                        list->VtxBuffer.Data[static_cast<std::size_t>(index) + command.VtxOffset];
                    rlColor4ub(static_cast<unsigned char>(vertex.col & 0xFFU),
                               static_cast<unsigned char>((vertex.col >> 8U) & 0xFFU),
                               static_cast<unsigned char>((vertex.col >> 16U) & 0xFFU),
                               static_cast<unsigned char>((vertex.col >> 24U) & 0xFFU));
                    rlTexCoord2f(vertex.uv.x, vertex.uv.y);
                    rlVertex2f(vertex.pos.x, vertex.pos.y);
                }
            }
            rlEnd();
            rlDrawRenderBatchActive();
        }
    }

    rlSetTexture(0);
    rlDisableScissorTest();
    rlEnableBackfaceCulling();
}

} // namespace

ImGuiRaylibBackend::ImGuiRaylibBackend() {
    if (!IsWindowReady()) {
        log(LogLevel::error, "The editor backend requires an initialized window.");
        return;
    }

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        log(LogLevel::error, "The editor backend could not create a Dear ImGui context.");
        return;
    }
    context_created_ = true;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "ic2de_raylib";
    io.BackendRendererName = "ic2de_rlgl";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Keyboard navigation is deliberately off: it claims the keyboard whenever
    // any panel is focused, which would take movement keys from the game.
    // Development tools must not leave state files beside packaged content.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        log(LogLevel::error, "The editor backend could not build its font atlas.");
        release();
        return;
    }

    const Image atlas{
        .data = pixels,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    const Texture2D font_texture = LoadTextureFromImage(atlas);
    if (font_texture.id == 0U) {
        log(LogLevel::error, "The editor backend could not upload its font atlas.");
        release();
        return;
    }
    font_texture_id_ = font_texture.id;
    io.Fonts->SetTexID(static_cast<ImTextureID>(font_texture_id_));
    ImGui::StyleColorsDark();
}

ImGuiRaylibBackend::~ImGuiRaylibBackend() { release(); }

void ImGuiRaylibBackend::release() noexcept {
    if (frame_open_) {
        ImGui::EndFrame();
        frame_open_ = false;
    }
    if (font_texture_id_ != 0U) {
        const Texture2D font_texture{
            .id = font_texture_id_,
            .width = 0,
            .height = 0,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        };
        UnloadTexture(font_texture);
        font_texture_id_ = 0U;
    }
    if (context_created_) {
        ImGui::DestroyContext();
        context_created_ = false;
    }
}

bool ImGuiRaylibBackend::available() const noexcept {
    return context_created_ && font_texture_id_ != 0U;
}

bool ImGuiRaylibBackend::new_frame() {
    if (!available()) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2{static_cast<float>(GetScreenWidth()),
                            static_cast<float>(GetScreenHeight())};
    io.DisplayFramebufferScale = ImVec2{1.0F, 1.0F};
    const float frame_seconds = GetFrameTime();
    io.DeltaTime = frame_seconds > 0.0F ? frame_seconds : 1.0F / 60.0F;

    io.AddMousePosEvent(static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY()));
    io.AddMouseButtonEvent(0, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    io.AddMouseButtonEvent(1, IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
    io.AddMouseButtonEvent(2, IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
    const Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.x != 0.0F || wheel.y != 0.0F) {
        io.AddMouseWheelEvent(wheel.x, wheel.y);
    }

    io.AddKeyEvent(ImGuiMod_Ctrl,
                   IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT));
    io.AddKeyEvent(ImGuiMod_Super, IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER));
    for (const KeyMapping& mapping : key_mappings) {
        io.AddKeyEvent(mapping.imgui_key, IsKeyDown(mapping.raylib_key));
    }
    for (int character = GetCharPressed(); character != 0; character = GetCharPressed()) {
        io.AddInputCharacter(static_cast<unsigned int>(character));
    }

    ImGui::NewFrame();
    frame_open_ = true;
    return true;
}

void ImGuiRaylibBackend::render() {
    if (!available() || !frame_open_) {
        return;
    }
    item_active_ = ImGui::IsAnyItemActive();
    ImGui::Render();
    frame_open_ = false;

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data != nullptr) {
        submit_draw_data(*draw_data);
    }
    apply_mouse_cursor();

    const ImGuiIO& io = ImGui::GetIO();
    wants_text_input_ = io.WantTextInput;
    wants_mouse_ = io.WantCaptureMouse;
}

bool ImGuiRaylibBackend::wants_text_input() const noexcept { return wants_text_input_; }
bool ImGuiRaylibBackend::item_active() const noexcept { return item_active_; }
bool ImGuiRaylibBackend::wants_mouse() const noexcept { return wants_mouse_; }

} // namespace ic2d
