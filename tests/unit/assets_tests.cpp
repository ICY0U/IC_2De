#include "ic2d/assets.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <raylib.h>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path unique_test_root() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("ic2de-texture-reload-" + std::to_string(suffix));
}

void test_file_texture_reload_preserves_handle_and_last_good_content() {
    const std::filesystem::path assets{IC2DE_RUNTIME_ASSET_DIRECTORY};
    const std::filesystem::path root = unique_test_root();
    const std::filesystem::path watched = root / "watched.png";
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(assets / "player-atlas.png", watched,
                               std::filesystem::copy_options::overwrite_existing);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "IC_2DE texture reload test");
    expect(IsWindowReady(), "The texture reload test requires a hidden graphics context.");

    if (IsWindowReady()) {
        ic2d::TextureAssets textures;
        const ic2d::TextureHandle handle = textures.acquire(watched);
        const auto initial = textures.info(handle);
        expect(initial && initial->width == 1122 && initial->height == 1402 &&
                   initial->revision == 1,
               "The initial watched texture must load with revision one.");

        std::filesystem::copy_file(assets / "tree-atlas.png", watched,
                                   std::filesystem::copy_options::overwrite_existing);
        const ic2d::TextureReloadSummary observing = textures.reload_changed_files();
        const ic2d::TextureReloadSummary reloaded = textures.reload_changed_files();
        const auto replacement = textures.info(handle);
        expect(observing.reloaded == 0,
               "A new file stamp must remain stable for one poll before upload.");
        expect(reloaded.watched == 1 && reloaded.changed == 1 && reloaded.reloaded == 1 &&
                   reloaded.failed == 0,
               "A stable file change must reload exactly one watched texture.");
        expect(textures.alive(handle) && replacement && replacement->width == 1536 &&
                   replacement->height == 1024 && replacement->revision == 2,
               "Reload must preserve the handle while replacing dimensions and revision.");

        {
            std::ofstream invalid{watched, std::ios::binary | std::ios::trunc};
            invalid << "not a valid image";
        }
        static_cast<void>(textures.reload_changed_files());
        const ic2d::TextureReloadSummary rejected = textures.reload_changed_files();
        const auto retained = textures.info(handle);
        expect(rejected.changed == 1 && rejected.reloaded == 0 && rejected.failed == 1,
               "An invalid stable replacement must report one failed reload.");
        expect(textures.alive(handle) && retained && retained->width == 1536 &&
                   retained->height == 1024 && retained->revision == 2,
               "A failed reload must preserve the last good GPU texture and handle.");

        textures.release(handle);
        expect(textures.loaded_texture_count() == 0,
               "The watched texture must still release through its original handle.");
        textures.shutdown();
        CloseWindow();
    }

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_file_texture_reload_preserves_handle_and_last_good_content();
    if (failures == 0) {
        std::cout << "All asset hot-reload tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
