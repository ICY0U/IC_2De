#include <doctest/doctest.h>

#include "ic2d/aseprite.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace {

void write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream stream{path};
    stream << contents;
}

TEST_CASE("imports golden json array") {
    const std::filesystem::path metadata =
        std::filesystem::path{IC2DE_TEST_FIXTURE_DIRECTORY} / "aseprite" / "directions.json";
    const ic2d::AsepriteImportResult imported = ic2d::import_aseprite_json(metadata, 60);
    CHECK_MESSAGE((imported.atlas_path == (metadata.parent_path() / "hero.png").lexically_normal()),
                  "The atlas path must resolve beside the metadata file.");
    CHECK_MESSAGE((imported.clips.size() == 4), "Every Aseprite frame tag must become one clip.");
    CHECK_MESSAGE((imported.clips[0].frames[0].source.x == 0.0F &&
                   imported.clips[0].frames[1].source.x == 16.0F),
                  "Forward tags must preserve frame-array order.");
    CHECK_MESSAGE((imported.clips[1].frames[0].source.x == 16.0F &&
                   imported.clips[1].frames[1].source.x == 0.0F),
                  "Reverse tags must reverse their inclusive frame range.");
    CHECK_MESSAGE((imported.clips[2].loop_mode == ic2d::AnimationLoopMode::ping_pong &&
                   imported.clips[3].loop_mode == ic2d::AnimationLoopMode::ping_pong),
                  "Both Aseprite ping-pong directions must map to engine ping-pong playback.");
    CHECK_MESSAGE((imported.clips[3].frames.front().source.x == 48.0F &&
                   imported.clips[3].frames.back().source.x == 16.0F),
                  "Ping-pong reverse must start at the authored range end.");
    CHECK_MESSAGE((imported.clips[0].frames[0].duration_ticks == 6 &&
                   imported.clips[0].frames[1].duration_ticks == 1 &&
                   imported.clips[2].frames[1].duration_ticks == 2 &&
                   imported.clips[2].frames[2].duration_ticks == 15),
                  "Millisecond durations must round once into deterministic fixed ticks.");
    CHECK_MESSAGE((imported.clips[0].frames[1].events.size() == 1 &&
                   imported.clips[0].frames[1].events.front() == "footstep"),
                  "The optional IC_2DE frame-event extension must survive import.");
    CHECK_MESSAGE(
        (imported.clips[0].frames[1].flip_x && imported.clips[1].frames[0].flip_x),
        "The optional horizontal-flip extension must follow a frame through tag ordering.");
    CHECK_MESSAGE(
        (imported.clips[0].frames[1].presentation_scale == 1.5F &&
         imported.clips[1].frames[0].presentation_scale == 1.5F &&
         imported.clips[0].frames[0].presentation_scale == 1.0F),
        "The optional presentation-scale extension must follow a frame and default to one.");
}

void test_rejects_hash_format_and_unsafe_images(const std::filesystem::path& root) {
    const std::filesystem::path hash_path = root / "hash.json";
    write_file(
        hash_path,
        R"({"frames":{"hero":{}},"meta":{"image":"hero.png","size":{"w":1,"h":1},"frameTags":[]}})");
    bool rejected = false;
    try {
        static_cast<void>(ic2d::import_aseprite_json(hash_path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("json-array") != std::string_view::npos;
    }
    CHECK_MESSAGE((rejected), "Hash-format exports must fail with the required CLI format.");

    const std::filesystem::path unsafe_path = root / "unsafe.json";
    write_file(
        unsafe_path,
        R"({"frames":[{"frame":{"x":0,"y":0,"w":1,"h":1},"duration":1}],"meta":{"image":"../hero.png","size":{"w":1,"h":1},"frameTags":[{"name":"idle","from":0,"to":0}]}})");
    rejected = false;
    try {
        static_cast<void>(ic2d::import_aseprite_json(unsafe_path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()}.find("safe relative") != std::string_view::npos;
    }
    CHECK_MESSAGE((rejected), "Atlas references must not escape the metadata directory.");
}

void test_rejects_unsupported_packing(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "rotated.json";
    write_file(
        path,
        R"({"frames":[{"frame":{"x":0,"y":0,"w":1,"h":1},"rotated":true,"duration":1}],"meta":{"image":"hero.png","size":{"w":1,"h":1},"frameTags":[{"name":"idle","from":0,"to":0,"direction":"forward"}]}})");
    bool rejected = false;
    try {
        static_cast<void>(ic2d::import_aseprite_json(path));
    } catch (const std::runtime_error& error) {
        rejected =
            std::string_view{error.what()}.find("Rotated or trimmed") != std::string_view::npos;
    }
    CHECK_MESSAGE((rejected), "Unsupported packed-frame transforms must fail at import.");
}

void test_imports_ic2de_once_loop_extension(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "once.json";
    write_file(
        path,
        R"({"frames":[{"frame":{"x":0,"y":0,"w":1,"h":1},"duration":100}],"meta":{"image":"hero.png","size":{"w":1,"h":1},"frameTags":[{"name":"death","from":0,"to":0,"direction":"forward","ic2d_loop_mode":"once"}]}})");
    const ic2d::AsepriteImportResult imported = ic2d::import_aseprite_json(path, 60);
    CHECK_MESSAGE((imported.clips.size() == 1 &&
                   imported.clips.front().loop_mode == ic2d::AnimationLoopMode::once),
                  "The IC_2DE tag extension must import terminal animation clips as one-shots.");
}

} // namespace
