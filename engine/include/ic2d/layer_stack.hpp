#pragma once

#include "ic2d/events.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace ic2d {

struct FixedLayerUpdate {
    std::uint64_t tick{0};
    float seconds{0.0F};
};

struct LayerId {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    [[nodiscard]] auto operator<=>(const LayerId&) const = default;
};

// One owned application layer. Regular layers update before overlays; events
// travel from the topmost overlay down until a layer reports that it handled
// the event.
class Layer {
public:
    virtual ~Layer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void on_attach() {}
    virtual void on_detach() noexcept {}
    virtual void on_fixed_update(const FixedLayerUpdate&) {}
    [[nodiscard]] virtual bool on_event(const EngineEvent&) { return false; }
};

// Owns layer lifetimes and applies structural changes only after the current
// update or event batch. A layer may therefore request pushes/removals from a
// callback without invalidating traversal.
class LayerStack final {
public:
    LayerStack();
    ~LayerStack();

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;
    LayerStack(LayerStack&&) = delete;
    LayerStack& operator=(LayerStack&&) = delete;

    [[nodiscard]] LayerId push_layer(std::unique_ptr<Layer> layer);
    [[nodiscard]] LayerId push_overlay(std::unique_ptr<Layer> layer);
    [[nodiscard]] bool remove(LayerId id);

    void fixed_update(const FixedLayerUpdate& update);
    void dispatch(std::span<const EngineEvent> events);

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
