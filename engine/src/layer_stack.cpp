#include "ic2d/layer_stack.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ic2d {

struct LayerStack::Impl {
    struct Entry {
        LayerId id{};
        bool overlay{false};
        std::unique_ptr<Layer> layer;
    };

    enum class TransitionKind {
        push,
        remove,
    };

    struct Transition {
        TransitionKind kind{TransitionKind::push};
        LayerId id{};
        bool overlay{false};
        std::unique_ptr<Layer> layer;
    };

    [[nodiscard]] bool removal_pending(const LayerId id) const noexcept {
        return std::ranges::any_of(pending, [id](const Transition& transition) {
            return transition.kind == TransitionKind::remove && transition.id == id;
        });
    }

    [[nodiscard]] bool knows(const LayerId id) const noexcept {
        const bool active =
            std::ranges::any_of(entries, [id](const Entry& entry) { return entry.id == id; });
        const bool waiting = std::ranges::any_of(pending, [id](const Transition& transition) {
            return transition.kind == TransitionKind::push && transition.id == id;
        });
        return (active || waiting) && !removal_pending(id);
    }

    void apply_push(Transition transition) {
        transition.layer->on_attach();
        Entry entry{
            .id = transition.id,
            .overlay = transition.overlay,
            .layer = std::move(transition.layer),
        };
        if (entry.overlay) {
            entries.push_back(std::move(entry));
        } else {
            entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(regular_count),
                           std::move(entry));
            ++regular_count;
        }
    }

    void apply_remove(const LayerId id) {
        const auto found = std::ranges::find(entries, id, &Entry::id);
        if (found == entries.end()) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(found - entries.begin());
        found->layer->on_detach();
        entries.erase(found);
        if (index < regular_count) {
            --regular_count;
        }
    }

    void apply_pending() {
        if (traversing || applying) {
            return;
        }
        applying = true;
        try {
            while (!pending.empty()) {
                std::vector<Transition> batch;
                batch.swap(pending);
                for (Transition& transition : batch) {
                    if (transition.kind == TransitionKind::push) {
                        apply_push(std::move(transition));
                    } else {
                        apply_remove(transition.id);
                    }
                }
            }
            applying = false;
        } catch (...) {
            applying = false;
            pending.clear();
            throw;
        }
    }

    template <typename Callback> void traverse(Callback&& callback) {
        if (traversing) {
            throw std::logic_error{"LayerStack traversal cannot be nested."};
        }
        traversing = true;
        try {
            callback();
            traversing = false;
            apply_pending();
        } catch (...) {
            traversing = false;
            pending.clear();
            throw;
        }
    }

    std::vector<Entry> entries;
    std::vector<Transition> pending;
    std::size_t regular_count{0};
    std::uint64_t next_id{1};
    bool traversing{false};
    bool applying{false};
};

LayerStack::LayerStack() : impl_{std::make_unique<Impl>()} {}

LayerStack::~LayerStack() {
    impl_->pending.clear();
    for (auto entry = impl_->entries.rbegin(); entry != impl_->entries.rend(); ++entry) {
        entry->layer->on_detach();
    }
}

LayerId LayerStack::push_layer(std::unique_ptr<Layer> layer) {
    if (!layer) {
        throw std::invalid_argument{"LayerStack cannot own a null layer."};
    }
    const LayerId id{impl_->next_id++};
    impl_->pending.push_back({
        .kind = Impl::TransitionKind::push,
        .id = id,
        .overlay = false,
        .layer = std::move(layer),
    });
    impl_->apply_pending();
    return id;
}

LayerId LayerStack::push_overlay(std::unique_ptr<Layer> layer) {
    if (!layer) {
        throw std::invalid_argument{"LayerStack cannot own a null overlay."};
    }
    const LayerId id{impl_->next_id++};
    impl_->pending.push_back({
        .kind = Impl::TransitionKind::push,
        .id = id,
        .overlay = true,
        .layer = std::move(layer),
    });
    impl_->apply_pending();
    return id;
}

bool LayerStack::remove(const LayerId id) {
    if (!id || !impl_->knows(id)) {
        return false;
    }
    impl_->pending.push_back({
        .kind = Impl::TransitionKind::remove,
        .id = id,
    });
    impl_->apply_pending();
    return true;
}

void LayerStack::fixed_update(const FixedLayerUpdate& update) {
    if (update.tick == 0 || !std::isfinite(update.seconds) || !(update.seconds > 0.0F)) {
        throw std::invalid_argument{"Layer updates require a non-zero tick and positive step."};
    }
    impl_->traverse([this, &update]() {
        for (const Impl::Entry& entry : impl_->entries) {
            entry.layer->on_fixed_update(update);
        }
    });
}

void LayerStack::dispatch(const std::span<const EngineEvent> events) {
    impl_->traverse([this, events]() {
        for (const EngineEvent& event : events) {
            for (auto entry = impl_->entries.rbegin(); entry != impl_->entries.rend(); ++entry) {
                if (entry->layer->on_event(event)) {
                    break;
                }
            }
        }
    });
}

std::size_t LayerStack::size() const noexcept { return impl_->entries.size(); }

} // namespace ic2d
