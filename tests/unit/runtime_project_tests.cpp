#include "ic2d/runtime_project.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream stream{path};
    stream << contents;
}

void test_loads_and_resolves_package_relative_content(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Content");
    write_file(root / "Content" / "test_area.scene", "schema=1\nid=test_area\n");
    write_file(root / "IC_2DE.runtime",
               "schema=1\nname=IC_2DE Shipped Test\nasset_directory=Content\n"
               "start_scene=test_area.scene\n");

    const auto project = ic2d::RuntimeProject::load(root / "IC_2DE.runtime");
    expect(project.schema_version() == 1, "Runtime project must retain its schema version.");
    expect(project.name() == "IC_2DE Shipped Test", "Runtime project must load its display name.");
    expect(project.start_scene_path() == (root / "Content" / "test_area.scene").lexically_normal(),
           "Start scene must resolve beneath the package asset directory.");
}

void test_rejects_unsupported_schema(const std::filesystem::path& root) {
    write_file(root / "bad-schema.runtime",
               "schema=2\nname=Bad\nasset_directory=Content\nstart_scene=test_area.scene\n");
    bool threw = false;
    try {
        static_cast<void>(ic2d::RuntimeProject::load(root / "bad-schema.runtime"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "Unsupported runtime project schemas must fail immediately.");
}

void test_rejects_path_escape(const std::filesystem::path& root) {
    write_file(root / "escape.runtime",
               "schema=1\nname=Escape\nasset_directory=Content\nstart_scene=../secret.scene\n");
    bool threw = false;
    try {
        static_cast<void>(ic2d::RuntimeProject::load(root / "escape.runtime"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "Runtime content must not escape the package asset directory.");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ic2de-runtime-project-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    test_loads_and_resolves_package_relative_content(root);
    test_rejects_unsupported_schema(root);
    test_rejects_path_escape(root);

    std::filesystem::remove_all(root);
    if (failures == 0) {
        std::cout << "All RuntimeProject tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
