// SPDX-License-Identifier: MIT
#include "Pack.h"
#include <json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("SkyEmote-diagnostics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Directory() { std::filesystem::create_directory(path); }
        ~Directory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    };
}
int main()
{
    try {
        using Json = nlohmann::json;
        auto document = Json::parse(R"({"schema":1,"id":"demo","name":"Demo","categories":[{"id":"social","name":"Social","sections":[{"id":"greetings","name":"Greetings","emotes":[{"id":"wave","name":"Wave","event":"IdleWave","mode":"once"}]}]}]})");
        document["required_files"] = {"meshes/demo.hkx"};
        document["graph"] = {{"variable", "DemoRevision"}, {"minimum", 2}};
        auto parsed = SkyEmote::ParsePack("demo.json", document.dump());
        Check(parsed.has_value(), "Installation metadata rejected");
        // Revision warnings distinguish unavailable inspection from missing and older markers.
        const auto& requirement = *parsed->graph;
        Check(!SkyEmote::CheckGraphRevision(requirement, false, {}).empty(), "Unavailable probe hidden");
        Check(SkyEmote::CheckGraphRevision(requirement, true, {}).find("missing") != std::string::npos,
            "Missing marker not identified");
        Check(!SkyEmote::CheckGraphRevision(requirement, true, 1).empty(), "Older graph accepted as current");
        Check(SkyEmote::CheckGraphRevision(requirement, true, 2).empty() &&
            SkyEmote::CheckGraphRevision(requirement, true, 3).empty(), "Current/newer graph warned");
        Check(SkyEmote::CheckGraphRevision({"ZeroRevision", 0}, true, 0).empty(), "Valid zero marker ignored");
        // File checks report the precise missing asset and recover on the next check after deployment.
        Directory directory;
        auto warnings = SkyEmote::CheckPackFiles(*parsed, directory.path);
        Check(warnings.size() == 1 && warnings[0].find("meshes/demo.hkx") != std::string::npos,
            "Missing file lost its name");
        std::filesystem::create_directory(directory.path / "meshes");
        { std::ofstream output(directory.path / "meshes/demo.hkx"); output << "fixture"; }
        Check(SkyEmote::CheckPackFiles(*parsed, directory.path).empty(), "Present file still warned");
        // Manifest paths remain UTF-8 when Windows uses a different system code page.
        document["required_files"] = {"meshes/舞蹈.hkx"};
        parsed = SkyEmote::ParsePack("unicode.json", document.dump());
        Check(parsed.has_value(), "Unicode requirement was rejected");
        { std::ofstream output(directory.path / std::filesystem::path(u8"meshes/舞蹈.hkx")); output << "fixture"; }
        Check(SkyEmote::CheckPackFiles(*parsed, directory.path).empty(), "UTF-8 requirement did not find its file");
        // Requirements cannot escape Data or use ambiguous Windows path spellings.
        for (const auto* name : {"../outside", "/absolute", "C:/absolute", "meshes\\demo.hkx", "a//b", "a/./b", "a/../b", "a/", "a./b", "a /b"}) {
            auto bad = document;
            bad["required_files"] = {name};
            Check(!SkyEmote::ParsePack("bad.json", bad.dump()), "Unsafe requirement path accepted");
        }
        for (const auto& minimum : {Json(-1), Json(1.5), Json(true), Json(2147483648LL)}) {
            auto bad = document;
            bad["graph"]["minimum"] = minimum;
            Check(!SkyEmote::ParsePack("bad.json", bad.dump()), "Invalid revision accepted");
        }
        std::cout << "Pack diagnostic checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
