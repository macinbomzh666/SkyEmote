// SPDX-License-Identifier: MIT
#include "Pack.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include <json.hpp>

namespace
{
    int checks = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
        ++checks;
    }

    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("SkyEmote-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        Directory()
        {
            Check(std::filesystem::create_directory(path), "Cannot create unique test directory");
        }

        ~Directory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        void Write(const std::filesystem::path& name, const std::string& text) const
        {
            std::ofstream file(path / name, std::ios::binary);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
            Check(!file.fail(), "Cannot write test manifest");
        }
    };
}

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "Expected bundled manifest path");
        std::ifstream input(argv[1], std::ios::binary);
        Check(input.is_open(), "Cannot read bundled manifest");
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        const auto fixture = nlohmann::json::parse(text);
        const auto manifest = [&](const std::string& id) {
            auto document = fixture;
            document["id"] = id;
            return document.dump();
        };

        // Verify filename order, case-insensitive extensions, nonrecursive filtering and malformed-file isolation.
        Directory mixed;
        mixed.Write("z.JSON", manifest("last"));
        mixed.Write("a.json", manifest("first"));
        mixed.Write("broken.json", "{");
        mixed.Write("ignored.txt", "{");
        std::filesystem::create_directory(mixed.path / "nested");
        mixed.Write("nested/hidden.json", text);
        std::filesystem::create_directory(mixed.path / "folder.json");
        auto loaded = SkyEmote::LoadPacks(mixed.path);
        Check(loaded.packs.size() == 2 && loaded.errors.size() == 1,
            "Bad/ignored entries affected valid packs");
        Check(loaded.packs.at(0).id == "first" && loaded.packs.at(1).id == "last",
            "Pack order must follow filenames");
        Check(loaded.errors.at(0).file == "broken.json" &&
            loaded.errors.at(0).location.starts_with("byte ") && !loaded.errors.at(0).reason.empty(),
            "Malformed file diagnostic lost its context");
        // Keep the loaded packs and their errors available for the menu.
        SkyEmote::ReloadPacks(mixed.path);
        Check(SkyEmote::GetPacks().packs.size() == 2 && SkyEmote::GetPacks().errors.size() == 1,
            "Stored catalogue lost packs or errors");
        // Report missing directories and files passed as directories.
        loaded = SkyEmote::LoadPacks(mixed.path / "missing");
        Check(loaded.packs.empty() && loaded.errors.size() == 1 && loaded.errors[0].file == "missing",
            "Missing directory needs a diagnostic");
        loaded = SkyEmote::LoadPacks(mixed.path / "a.json");
        Check(loaded.packs.empty() && loaded.errors.size() == 1, "A file is not a pack directory");

        // Reject all files sharing a pack ID while retaining unrelated valid packs.
        Directory conflicts;
        conflicts.Write("one.json", manifest("same"));
        conflicts.Write("two.json", manifest("same"));
        conflicts.Write("valid.json", manifest("keep"));
        loaded = SkyEmote::LoadPacks(conflicts.path);
        Check(loaded.packs.size() == 1 && loaded.packs[0].id == "keep" && loaded.errors.size() == 2,
            "All conflicting pack IDs must be excluded");
        for (const auto& error : loaded.errors) {
            Check((error.file == "one.json" || error.file == "two.json") && error.location == "/id" &&
                error.reason.find("same") != std::string::npos, "Duplicate diagnostic missing identity");
        }

        // Load large valid manifests and retain Unicode names when reporting malformed files.
        Directory invalid;
        auto huge = fixture;
        huge["id"] = "huge";
        huge["name"] = std::string(2 * 1024 * 1024, 'A');
        invalid.Write("huge.json", huge.dump());
        invalid.Write(std::filesystem::path(u8"坏.json"), "{");
        invalid.Write("valid.json", text);
        loaded = SkyEmote::LoadPacks(invalid.path);
        Check(loaded.packs.size() == 2 && loaded.errors.size() == 1, "Large valid manifest was not loaded");
        Check(loaded.packs.at(0).id == "huge" && loaded.packs[0].name.size() == 2 * 1024 * 1024,
            "Large manifest was truncated");
        Check(loaded.errors.at(0).file == "坏.json", "Unicode diagnostic filename was not preserved");

        // Empty directories are valid. Large directories must retain every pack.
        Directory manyFiles;
        loaded = SkyEmote::LoadPacks(manyFiles.path);
        Check(loaded.packs.empty() && loaded.errors.empty(), "Empty directory should be valid");
        // Replacing the catalogue also removes old packs and error messages.
        SkyEmote::ReloadPacks(manyFiles.path);
        Check(SkyEmote::GetPacks().packs.empty() && SkyEmote::GetPacks().errors.empty(),
            "Reloading left a stale catalogue");
        for (int i = 0; i < 128; ++i) {
            manyFiles.Write(std::to_string(i) + ".json", manifest("p" + std::to_string(i)));
        }
        loaded = SkyEmote::LoadPacks(manyFiles.path);
        Check(loaded.packs.size() == 128 && loaded.errors.empty(), "128 manifests should load");
        manyFiles.Write("overflow.json", manifest("overflow"));
        loaded = SkyEmote::LoadPacks(manyFiles.path);
        Check(loaded.packs.size() == 129 && loaded.errors.empty(),
            "All manifests beyond the old 128 cap must load");
        std::cout << checks << " pack-file checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
