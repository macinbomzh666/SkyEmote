// SPDX-License-Identifier: MIT
#include "FacePresets.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, std::string_view message)
    {
        if (!condition) throw std::runtime_error(std::string(message));
    }

    struct Directory
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("SkyEmote-FacePresets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        Directory()
        {
            Check(std::filesystem::create_directory(path), "Cannot create preset test directory");
        }

        ~Directory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        void Write(const std::filesystem::path& name, std::string_view contents) const
        {
            std::ofstream file(path / name, std::ios::binary);
            file << contents;
            file.close();
            Check(!file.fail(), "Cannot write test preset file");
        }
    };

    void CheckLibrary()
    {
        using namespace SkyEmote;
        const Directory plugins;
        const auto face = std::filesystem::path("PosePresets/Face");
        std::filesystem::create_directories(plugins.path / face);
        constexpr std::string_view wink = R"({"schema":1,"type":"face","source":"GModPose",
            "name":"Wink \"café\"","pose":{"mood":{"name":"happy","strength":0.75},
            "weights":{"blink_left":1,"blink_right":0,"brow_down_left":0.5,"w":0.25}}})";
        plugins.Write(face / "a-wink.json", wink);
        plugins.Write(face / "b-duplicate.json", R"({"schema":1,"type":"face","name":"wink \"café\"",
            "pose":{"weights":{"blink_left":0}}})");
        plugins.Write(face / "sparse.json", R"({"schema":1,"type":"face","name":"Sparse","category":"premade",
            "pose":{"weights":{"eee":0}}})");
        plugins.Write(face / "previous.gmodpose", wink);
        plugins.Write(face / "text.json", "GModPose face pose 1\n");
        plugins.Write(face / "unknown.json", R"({"schema":1,"type":"face","name":"Unknown",
            "pose":{"weights":{"imaginary":1}}})");
        plugins.Write(face / "invalid.json", R"({"schema":1,"type":"face","name":"Bad weight",
            "pose":{"weights":{"blink_left":1.5}}})");
        plugins.Write(face / "duplicate-key.json", R"({"schema":1,"type":"face","name":"Duplicate",
            "pose":{"weights":{"blink_left":1,"blink_left":0}}})");
        LoadCustomExpressionPresets(plugins.path);
        const auto presets = CustomExpressionPresets();
        Check(presets->size() == 2, "Loaded a foreign, malformed, or duplicate face preset");
        const auto& sparse = presets->front();
        Check(sparse.label == "Sparse" && sparse.premade && !sparse.pose.mood &&
            !sparse.pose.eyelids[0] && sparse.pose.phonemes[5] == 0.0F, "Sparse preset lost category or zero");
        const auto& saved = presets->back();
        Check(saved.key == L"face:77696e6b2022636166c3a922" && saved.source == "GModPose" && !saved.premade,
            "Custom preset lost its key, source, or category");
        Check(saved.pose.mood && saved.pose.mood->index == 3 && saved.pose.mood->strength == 0.75F,
            "Mood mapping changed");
        Check(saved.pose.eyelids[0] == 1.0F && saved.pose.eyelids[1] == 0.0F &&
            saved.pose.modifiers.front() == 0.5F && !saved.pose.modifiers.back() && saved.pose.phonemes.back() == 0.25F,
            "Named channels lost their weights or omission");

        SetFaceOverrideSettings({true, saved.key});
        plugins.Write(face / "long.json", "{\"schema\":1,\"type\":\"face\",\"name\":\"" +
            std::string(PresetNameLimit, 'a') + "\",\"pose\":{\"weights\":{\"eee\":0}}}");
        LoadCustomExpressionPresets(plugins.path);
        const auto updated = CustomExpressionPresets();
        Check(updated->size() == 3 && updated->front().key.size() + 1 == ExpressionPresetKeyCapacity,
            "Saved key buffer does not fit the longest preset name");
        Check(GetFaceOverrideSettings()->preset == saved.key, "Reload changed the saved override selection");

        for (const auto* file : {"a-wink.json", "b-duplicate.json", "sparse.json", "long.json"}) {
            std::filesystem::remove(plugins.path / face / file);
        }
        LoadCustomExpressionPresets(plugins.path);
        Check(CustomExpressionPresets()->empty(), "Deleted presets stayed available");
    }
}

int main(int argc, char** argv)
{
    try {
        using namespace SkyEmote;
        Check(argc == 2, "Expected the bundled catalogue path");
        const auto plugins = std::filesystem::path(argv[1]).parent_path().parent_path().parent_path();
        LoadBuiltInExpressionPresets(plugins);
        const auto premades = BuiltInExpressionPresets();
        Check(premades.size() == 19 && premades.front().key == L"smile" && premades.front().premade,
            "Bundled presets lost their order, category, or stable key");
        const auto* calm = FindExpressionPreset({}, L"calm");
        Check(calm && !calm->pose.mood && !calm->pose.eyelids[0], "Bundled sparse pose became a full override");
        CheckLibrary();
        std::cout << "Face preset checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
