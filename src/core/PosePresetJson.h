// SPDX-License-Identifier: MIT
#pragma once

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <istream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PosePresetJson
{
    using Json = nlohmann::json;
    enum class Category { Custom, Premade };
    constexpr std::array categories{"Custom", "Premade"};
    constexpr std::array<std::string_view, 8> moods{
        "neutral", "anger", "fear", "happy", "sad", "surprise", "puzzled", "disgusted"
    };
    constexpr std::array<std::string_view, 30> channels{
        "blink_left", "blink_right", "brow_down_left", "brow_down_right", "brow_in_left", "brow_in_right",
        "brow_up_left", "brow_up_right", "look_down", "look_left", "look_right", "look_up",
        "squint_left", "squint_right", "aah", "big_aah", "bmp", "ch_j_sh", "dst", "eee", "eh", "fv",
        "i", "k", "n", "oh", "ooh_q", "r", "th", "w"
    };

    struct Mood { unsigned index{}; float strength{}; };
    struct Face
    {
        std::optional<Mood> mood;
        std::array<std::optional<float>, channels.size()> weights{};
    };

    inline bool ValidName(std::string_view value)
    {
        return !value.empty() && value.size() < 64 &&
            std::ranges::none_of(value, [](unsigned char ch) { return ch < 32 || ch == 127; });
    }

    inline Json Read(std::istream& input)
    {
        bool valid = true;
        std::array<std::set<std::string>, 12> keys;
        auto result = Json::parse(input, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth >= static_cast<int>(keys.size())) return valid = false;
            if (event == Json::parse_event_t::object_start) keys[depth].clear();
            if (event == Json::parse_event_t::key && !keys[depth - 1].insert(value.get<std::string>()).second) {
                return valid = false;
            }
            return true;
        }, false);
        return valid ? std::move(result) : Json{};
    }

    inline const Json& Field(const Json& object, std::string_view key)
    {
        static const Json missing;
        if (!object.is_object()) return missing;
        const auto field = object.find(std::string(key));
        return field == object.end() ? missing : *field;
    }

    inline bool StringIs(const Json& value, std::string_view expected)
    {
        return value.is_string() && value.get_ref<const std::string&>() == expected;
    }

    inline bool Header(const Json& document, std::string_view type, std::string& name, Category& category)
    {
        const auto& schema = Field(document, "schema");
        const auto& label = Field(document, "name");
        if (!schema.is_number_integer() || schema != 1 || !StringIs(Field(document, "type"), type) ||
            !label.is_string() || !ValidName(label.get_ref<const std::string&>())) return false;
        category = Category::Custom;
        if (document.contains("category")) {
            const auto& group = document["category"];
            if (StringIs(group, "premade")) category = Category::Premade;
            else if (!StringIs(group, "custom")) return false;
        }
        if (document.contains("source")) {
            const auto& source = document["source"];
            if (!source.is_string() || !ValidName(source.get_ref<const std::string&>())) return false;
        }
        name = label.get<std::string>();
        return true;
    }

    inline Json Document(std::string_view type, std::string_view name, Category category, Json pose)
    {
        return {{"schema", 1}, {"type", type}, {"name", name},
            {"category", category == Category::Custom ? "custom" : "premade"}, {"pose", std::move(pose)}};
    }

    template <typename T>
    bool Number(const Json& value, T& output, double low, double high)
    {
        if (!value.is_number()) return false;
        const double number = value.get<double>();
        if (!std::isfinite(number) || number < low || number > high) return false;
        output = static_cast<T>(number);
        return true;
    }

    inline bool DecodeFace(const Json& pose, Face& output)
    {
        if (!pose.is_object()) return false;
        Face parsed;
        for (const auto& [key, value] : pose.items()) {
            if (key == "mood") {
                const auto& name = Field(value, "name");
                if (!name.is_string() || value.size() != 2) return false;
                const auto slot = std::ranges::find(moods, name.get_ref<const std::string&>());
                float strength;
                if (slot == moods.end() || !Number(Field(value, "strength"), strength, 0, 1)) return false;
                parsed.mood = Mood{static_cast<unsigned>(slot - moods.begin()), strength};
            } else if (key == "weights" && value.is_object() && !value.empty()) {
                for (const auto& [channel, weight] : value.items()) {
                    const auto slot = std::ranges::find(channels, channel);
                    float amount;
                    if (slot == channels.end() || !Number(weight, amount, 0, 1)) return false;
                    parsed.weights[slot - channels.begin()] = amount;
                }
            } else {
                return false;
            }
        }
        if (pose.empty()) return false;
        output = parsed;
        return true;
    }

    inline Json EncodeFace(const Face& face)
    {
        auto pose = Json::object();
        if (face.mood) pose["mood"] = {{"name", moods[face.mood->index]}, {"strength", face.mood->strength}};
        for (std::size_t i = 0; i < face.weights.size(); ++i) {
            if (face.weights[i]) pose["weights"][channels[i]] = *face.weights[i];
        }
        return pose;
    }

    inline std::vector<std::filesystem::path> Files(const std::filesystem::path& folder, std::string_view extension = ".json")
    {
        std::vector<std::filesystem::path> paths;
        std::error_code error;
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            std::error_code entryError;
            if (it->path().extension() == extension && it->is_regular_file(entryError) &&
                it->file_size(entryError) <= 4 * 1024 * 1024) paths.push_back(it->path());
        }
        std::ranges::sort(paths);
        return paths;
    }
}
