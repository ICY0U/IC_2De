#pragma once

namespace ic2d {

// Thin wrapper around RenderDoc's in-application API (renderdoc_app.h). Entirely
// inert unless the process was launched with renderdoc.dll already injected, e.g.
// via tools/launch-renderdoc.ps1 — GetModuleHandle finds nothing otherwise, so
// this is always safe to construct and tick even outside RenderDoc.
class RenderDocCapture {
public:
    RenderDocCapture();

    [[nodiscard]] bool available() const { return api_ != nullptr; }

    // Call once per frame with that frame's duration in seconds. Triggers a
    // capture once `interval_seconds` of wall-clock time have elapsed since
    // the last one, regardless of frame rate (uncapped debug builds can run
    // at 1000+ fps, so a frame-count interval would capture far too often). A
    // value <= 0 disables automatic captures.
    void tick(double interval_seconds, double frame_seconds);

private:
    void* api_{nullptr};
    double seconds_since_capture_{0.0};
};

} // namespace ic2d
