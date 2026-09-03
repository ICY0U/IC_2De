#include "ic2d/assets.hpp"

#include "assets/texture_assets_internal.hpp"
#include "ic2d/core/log.hpp"

#include <limits>
#include <string>
#include <utility>

#include <raylib.h>

namespace ic2d {
namespace {

[[nodiscard]] std::string normalized_key(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

[[nodiscard]] std::uint32_t next_generation(const std::uint32_t current) noexcept {
    const std::uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

[[nodiscard]] std::uint64_t next_revision(const std::uint64_t current) noexcept {
    const std::uint64_t next = current + 1U;
    return next == 0U ? 1U : next;
}

[[nodiscard]] TextureFileStamp file_stamp(const std::filesystem::path& path) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return {};
    }
    const std::filesystem::file_time_type write_time =
        std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    return {.write_time = write_time, .size = size, .exists = true};
}

} // namespace

TextureAssets::Impl::Slot* TextureAssets::Impl::resolve(const TextureHandle handle) noexcept {
    if (!handle || handle.index > slots.size()) {
        return nullptr;
    }
    Slot& slot = slots[handle.index - 1U];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const TextureAssets::Impl::Slot*
TextureAssets::Impl::resolve(const TextureHandle handle) const noexcept {
    if (!handle || handle.index > slots.size()) {
        return nullptr;
    }
    const Slot& slot = slots[handle.index - 1U];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

TextureAssets::TextureAssets() : impl_{std::make_unique<Impl>()} {
    Image checker = GenImageChecked(16, 16, 4, 4, MAGENTA, BLACK);
    Texture2D texture = LoadTextureFromImage(checker);
    UnloadImage(checker);

    Impl::Slot fallback_slot{
        .texture = texture,
        .generation = 1,
        .reference_count = std::numeric_limits<std::size_t>::max(),
        .source = "<fallback-checker>",
        .occupied = true,
        .fallback = true,
    };
    impl_->slots.push_back(std::move(fallback_slot));
    impl_->fallback_handle = {.index = 1, .generation = 1};
}

TextureAssets::~TextureAssets() { shutdown(); }

void TextureAssets::shutdown() noexcept {
    for (Impl::Slot& slot : impl_->slots) {
        if (slot.occupied && IsTextureValid(slot.texture)) {
            UnloadTexture(slot.texture);
            slot.texture = {};
            slot.occupied = false;
        }
    }
    impl_->path_cache.clear();
    impl_->free_indices.clear();
}

TextureHandle TextureAssets::acquire(const std::filesystem::path& path,
                                     const TextureSampling sampling) {
    const std::string key = normalized_key(path);
    if (const auto cached = impl_->path_cache.find(key); cached != impl_->path_cache.end()) {
        Impl::Slot& slot = impl_->slots[cached->second];
        ++slot.reference_count;
        return {.index = cached->second + 1U, .generation = slot.generation};
    }

    Texture2D texture = LoadTexture(key.c_str());
    if (!IsTextureValid(texture)) {
        log(LogLevel::warning, "Texture load failed; using fallback: " + key);
        return impl_->fallback_handle;
    }
    SetTextureFilter(texture, sampling == TextureSampling::pixel ? TEXTURE_FILTER_POINT
                                                                 : TEXTURE_FILTER_BILINEAR);

    std::uint32_t slot_index = 0;
    if (!impl_->free_indices.empty()) {
        slot_index = impl_->free_indices.back();
        impl_->free_indices.pop_back();
    } else {
        slot_index = static_cast<std::uint32_t>(impl_->slots.size());
        impl_->slots.emplace_back();
    }

    Impl::Slot& slot = impl_->slots[slot_index];
    slot.texture = texture;
    slot.reference_count = 1;
    slot.revision = 1;
    slot.source = key;
    slot.sampling = sampling;
    slot.observed_stamp = file_stamp(key);
    slot.processed_stamp = slot.observed_stamp;
    slot.stable_observations = 0;
    slot.occupied = true;
    slot.fallback = false;
    slot.file_backed = true;
    impl_->path_cache.emplace(key, slot_index);
    return {.index = slot_index + 1U, .generation = slot.generation};
}

TextureHandle TextureAssets::create_checker(std::string name, const int width, const int height,
                                            const int cell_size, const ColorRgba8 first,
                                            const ColorRgba8 second,
                                            const TextureSampling sampling) {
    if (name.empty() || width <= 0 || height <= 0 || cell_size <= 0) {
        log(LogLevel::warning, "Invalid generated checker request; using fallback.");
        return impl_->fallback_handle;
    }

    const std::string key = "<generated-checker>/" + name;
    if (const auto cached = impl_->path_cache.find(key); cached != impl_->path_cache.end()) {
        Impl::Slot& slot = impl_->slots[cached->second];
        ++slot.reference_count;
        return {.index = cached->second + 1U, .generation = slot.generation};
    }

    const Color first_color{first.red, first.green, first.blue, first.alpha};
    const Color second_color{second.red, second.green, second.blue, second.alpha};
    Image image = GenImageChecked(width, height, cell_size, cell_size, first_color, second_color);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (!IsTextureValid(texture)) {
        log(LogLevel::warning, "Generated checker upload failed; using fallback: " + name);
        return impl_->fallback_handle;
    }
    SetTextureFilter(texture, sampling == TextureSampling::pixel ? TEXTURE_FILTER_POINT
                                                                 : TEXTURE_FILTER_BILINEAR);

    std::uint32_t slot_index = 0;
    if (!impl_->free_indices.empty()) {
        slot_index = impl_->free_indices.back();
        impl_->free_indices.pop_back();
    } else {
        slot_index = static_cast<std::uint32_t>(impl_->slots.size());
        impl_->slots.emplace_back();
    }

    Impl::Slot& slot = impl_->slots[slot_index];
    slot.texture = texture;
    slot.reference_count = 1;
    slot.revision = 1;
    slot.source = key;
    slot.sampling = sampling;
    slot.observed_stamp = {};
    slot.processed_stamp = {};
    slot.stable_observations = 0;
    slot.occupied = true;
    slot.fallback = false;
    slot.file_backed = false;
    impl_->path_cache.emplace(key, slot_index);
    return {.index = slot_index + 1U, .generation = slot.generation};
}

TextureHandle TextureAssets::create_radial_gradient(std::string name, const int width,
                                                    const int height, const ColorRgba8 inner,
                                                    const ColorRgba8 outer,
                                                    const TextureSampling sampling) {
    if (name.empty() || width <= 0 || height <= 0) {
        log(LogLevel::warning, "Invalid generated radial-gradient request; using fallback.");
        return impl_->fallback_handle;
    }

    const std::string key = "<generated-radial>/" + name;
    if (const auto cached = impl_->path_cache.find(key); cached != impl_->path_cache.end()) {
        Impl::Slot& slot = impl_->slots[cached->second];
        ++slot.reference_count;
        return {.index = cached->second + 1U, .generation = slot.generation};
    }

    const Color inner_color{inner.red, inner.green, inner.blue, inner.alpha};
    const Color outer_color{outer.red, outer.green, outer.blue, outer.alpha};
    Image image = GenImageGradientRadial(width, height, 0.0F, inner_color, outer_color);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (!IsTextureValid(texture)) {
        log(LogLevel::warning, "Generated radial-gradient upload failed; using fallback: " + name);
        return impl_->fallback_handle;
    }
    SetTextureFilter(texture, sampling == TextureSampling::pixel ? TEXTURE_FILTER_POINT
                                                                 : TEXTURE_FILTER_BILINEAR);

    std::uint32_t slot_index = 0;
    if (!impl_->free_indices.empty()) {
        slot_index = impl_->free_indices.back();
        impl_->free_indices.pop_back();
    } else {
        slot_index = static_cast<std::uint32_t>(impl_->slots.size());
        impl_->slots.emplace_back();
    }

    Impl::Slot& slot = impl_->slots[slot_index];
    slot.texture = texture;
    slot.reference_count = 1;
    slot.revision = 1;
    slot.source = key;
    slot.sampling = sampling;
    slot.observed_stamp = {};
    slot.processed_stamp = {};
    slot.stable_observations = 0;
    slot.occupied = true;
    slot.fallback = false;
    slot.file_backed = false;
    impl_->path_cache.emplace(key, slot_index);
    return {.index = slot_index + 1U, .generation = slot.generation};
}

void TextureAssets::release(const TextureHandle handle) noexcept {
    Impl::Slot* slot = impl_->resolve(handle);
    if (!slot || slot->fallback) {
        return;
    }
    if (slot->reference_count > 1) {
        --slot->reference_count;
        return;
    }

    if (IsTextureValid(slot->texture)) {
        UnloadTexture(slot->texture);
    }
    impl_->path_cache.erase(slot->source);
    slot->texture = {};
    slot->reference_count = 0;
    slot->revision = 0;
    slot->source.clear();
    slot->observed_stamp = {};
    slot->processed_stamp = {};
    slot->stable_observations = 0;
    slot->occupied = false;
    slot->file_backed = false;
    slot->generation = next_generation(slot->generation);
    impl_->free_indices.push_back(handle.index - 1U);
}

TextureReloadSummary TextureAssets::reload_changed_files() {
    TextureReloadSummary summary{};
    for (Impl::Slot& slot : impl_->slots) {
        if (!slot.occupied || slot.fallback || !slot.file_backed) {
            continue;
        }
        ++summary.watched;

        const TextureFileStamp current = file_stamp(slot.source);
        if (current != slot.observed_stamp) {
            slot.observed_stamp = current;
            slot.stable_observations = 1;
            continue;
        }
        if (current == slot.processed_stamp) {
            slot.stable_observations = 0;
            continue;
        }
        if (slot.stable_observations == 0) {
            slot.stable_observations = 1;
            continue;
        }

        ++summary.changed;
        slot.processed_stamp = current;
        slot.stable_observations = 0;
        if (!current.exists) {
            ++summary.failed;
            log(LogLevel::warning,
                "Texture hot reload kept the last good image; file is unavailable: " + slot.source);
            continue;
        }

        Texture2D replacement = LoadTexture(slot.source.c_str());
        if (!IsTextureValid(replacement)) {
            ++summary.failed;
            log(LogLevel::warning,
                "Texture hot reload kept the last good image; replacement is invalid: " +
                    slot.source);
            continue;
        }
        SetTextureFilter(replacement, slot.sampling == TextureSampling::pixel
                                          ? TEXTURE_FILTER_POINT
                                          : TEXTURE_FILTER_BILINEAR);

        const Texture2D previous = slot.texture;
        slot.texture = replacement;
        slot.revision = next_revision(slot.revision);
        if (IsTextureValid(previous)) {
            UnloadTexture(previous);
        }
        ++summary.reloaded;
        log(LogLevel::info,
            "Texture hot reloaded revision " + std::to_string(slot.revision) + ": " + slot.source);
    }
    return summary;
}

TextureHandle TextureAssets::fallback() const noexcept { return impl_->fallback_handle; }

bool TextureAssets::alive(const TextureHandle handle) const noexcept {
    return impl_->resolve(handle) != nullptr;
}

std::optional<TextureInfo> TextureAssets::info(const TextureHandle handle) const {
    const Impl::Slot* slot = impl_->resolve(handle);
    if (!slot) {
        return std::nullopt;
    }
    return TextureInfo{
        .width = slot->texture.width,
        .height = slot->texture.height,
        .source = slot->source,
        .reference_count = slot->reference_count,
        .revision = slot->revision,
        .fallback = slot->fallback,
    };
}

std::size_t TextureAssets::loaded_texture_count() const noexcept {
    return impl_->path_cache.size();
}

} // namespace ic2d
