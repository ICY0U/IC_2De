#include "ic2d/layer_stack.hpp"

#include <limits>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class RecordingLayer final : public ic2d::Layer {
public:
    RecordingLayer(
        std::string layer_name,
        std::vector<std::string>& log,
        const bool handles_events = false,
        std::function<void()> on_event_action = {}
    )
        : name_{std::move(layer_name)},
          log_{&log},
          handles_events_{handles_events},
          on_event_action_{std::move(on_event_action)} {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    void on_attach() override { log_->push_back("attach:" + name_); }
    void on_detach() noexcept override { log_->push_back("detach:" + name_); }

    void on_fixed_update(const ic2d::FixedLayerUpdate&) override {
        log_->push_back("update:" + name_);
    }

    [[nodiscard]] bool on_event(const ic2d::EngineEvent&) override {
        log_->push_back("event:" + name_);
        if (on_event_action_) {
            std::function<void()> action = std::move(on_event_action_);
            action();
        }
        return handles_events_;
    }

private:
    std::string name_;
    std::vector<std::string>* log_{nullptr};
    bool handles_events_{false};
    std::function<void()> on_event_action_;
};

[[nodiscard]] ic2d::EngineEvent test_event() {
    return ic2d::SceneAnimationEvent{
        .entity_uuid = {42},
        .entity_id = "player",
        .clip_id = "walk-east",
        .name = "step",
        .frame_index = 1,
    };
}

void test_update_and_event_order() {
    std::vector<std::string> log;
    {
        ic2d::LayerStack stack;
        static_cast<void>(stack.push_layer(std::make_unique<RecordingLayer>("world", log)));
        static_cast<void>(stack.push_layer(std::make_unique<RecordingLayer>("gameplay", log)));
        static_cast<void>(stack.push_overlay(std::make_unique<RecordingLayer>("tools", log)));
        log.clear();

        stack.fixed_update({.tick = 1, .seconds = 1.0F / 60.0F});
        const std::vector<ic2d::EngineEvent> events{test_event()};
        stack.dispatch(events);

        const std::vector<std::string> expected{
            "update:world", "update:gameplay", "update:tools",
            "event:tools", "event:gameplay", "event:world",
        };
        expect(log == expected,
               "Layers must update bottom-up and receive events top-down.");
    }
    expect(log.size() >= 3 && log[log.size() - 3] == "detach:tools" &&
               log[log.size() - 2] == "detach:gameplay" &&
               log.back() == "detach:world",
           "LayerStack destruction must detach owned layers from top to bottom.");
}

void test_handled_event_stops_propagation() {
    std::vector<std::string> log;
    ic2d::LayerStack stack;
    static_cast<void>(stack.push_layer(std::make_unique<RecordingLayer>("world", log)));
    static_cast<void>(stack.push_overlay(
        std::make_unique<RecordingLayer>("modal", log, true)));
    log.clear();

    const std::vector<ic2d::EngineEvent> events{test_event()};
    stack.dispatch(events);
    expect(log == std::vector<std::string>{"event:modal"},
           "A handled event must not reach lower layers.");
}

void test_structural_changes_are_deferred_until_after_dispatch() {
    std::vector<std::string> log;
    ic2d::LayerStack stack;
    const ic2d::LayerId world =
        stack.push_layer(std::make_unique<RecordingLayer>("world", log));
    static_cast<void>(stack.push_overlay(std::make_unique<RecordingLayer>(
        "tools", log, false, [&stack, &log, world]() {
            static_cast<void>(stack.remove(world));
            static_cast<void>(stack.push_overlay(
                std::make_unique<RecordingLayer>("console", log)));
        })));
    log.clear();

    const std::vector<ic2d::EngineEvent> events{test_event()};
    stack.dispatch(events);
    const std::vector<std::string> first_dispatch{
        "event:tools", "event:world", "detach:world", "attach:console",
    };
    expect(log == first_dispatch && stack.size() == 2,
           "Push and removal requests must apply after the active event batch.");

    log.clear();
    stack.dispatch(events);
    expect(log == std::vector<std::string>{"event:console", "event:tools"},
           "The next event batch must observe the committed layer order.");
}

void test_rejects_invalid_requests() {
    ic2d::LayerStack stack;
    bool null_rejected = false;
    try {
        static_cast<void>(stack.push_layer(nullptr));
    } catch (const std::invalid_argument&) {
        null_rejected = true;
    }
    expect(null_rejected, "A null owned layer must be rejected.");
    expect(!stack.remove({999}), "Removing an unknown layer id must report failure.");

    bool update_rejected = false;
    try {
        stack.fixed_update({});
    } catch (const std::invalid_argument&) {
        update_rejected = true;
    }
    expect(update_rejected, "Invalid fixed-layer update timing must be rejected.");

    bool non_finite_rejected = false;
    try {
        stack.fixed_update({
            .tick = 1,
            .seconds = std::numeric_limits<float>::infinity(),
        });
    } catch (const std::invalid_argument&) {
        non_finite_rejected = true;
    }
    expect(non_finite_rejected, "Non-finite layer update timing must be rejected.");
}

} // namespace

int main() {
    test_update_and_event_order();
    test_handled_event_stops_propagation();
    test_structural_changes_are_deferred_until_after_dispatch();
    test_rejects_invalid_requests();

    if (failures == 0) {
        std::cout << "Layer stack tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
