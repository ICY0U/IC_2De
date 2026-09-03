#include <doctest/doctest.h>

#include "ic2d/runtime_project.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace {

// Each case gets its own directory. The previous harness shared one across all
// three, which was safe only because a single main() ran them in a fixed order;
// individual cases can be run on their own now, so sharing would make a case
// depend on whether another had run first.
[[nodiscard]] std::filesystem::path test_root(const std::string_view name) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ic2de-runtime-project-tests" / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream stream{path};
    stream << contents;
}

} // namespace

TEST_CASE("loads and resolves package relative content") {
    const std::filesystem::path root = test_root("resolves-content");
    std::filesystem::create_directories(root / "Content");
    write_file(root / "Content" / "test_area.scene", "schema=1\nid=test_area\n");
    write_file(root / "IC_2DE.runtime",
               "schema=1\nname=IC_2DE Shipped Test\nasset_directory=Content\n"
               "start_scene=test_area.scene\n");

    const auto project = ic2d::RuntimeProject::load(root / "IC_2DE.runtime");
    CHECK_MESSAGE((project.schema_version() == 1),
                  "Runtime project must retain its schema version.");
    CHECK_MESSAGE((project.name() == "IC_2DE Shipped Test"),
                  "Runtime project must load its display name.");
    CHECK_MESSAGE(
        (project.start_scene_path() == (root / "Content" / "test_area.scene").lexically_normal()),
        "Start scene must resolve beneath the package asset directory.");
}

TEST_CASE("rejects unsupported schema") {
    const std::filesystem::path root = test_root("bad-schema");
    write_file(root / "bad-schema.runtime",
               "schema=2\nname=Bad\nasset_directory=Content\nstart_scene=test_area.scene\n");

    CHECK_THROWS_AS(static_cast<void>(ic2d::RuntimeProject::load(root / "bad-schema.runtime")),
                    std::runtime_error);
}

TEST_CASE("rejects path escape") {
    // Content that resolves outside the package directory would let a shipped
    // manifest read arbitrary files, so this has to fail at load rather than
    // when the path is first used.
    const std::filesystem::path root = test_root("path-escape");
    write_file(root / "escape.runtime",
               "schema=1\nname=Escape\nasset_directory=Content\nstart_scene=../secret.scene\n");

    CHECK_THROWS_AS(static_cast<void>(ic2d::RuntimeProject::load(root / "escape.runtime")),
                    std::runtime_error);
}
