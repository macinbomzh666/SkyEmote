// SPDX-License-Identifier: MIT
#include "Pack.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <limits>

#include <json.hpp>

namespace SkyEmote
{
    namespace
    {
        PackCatalogue loadedPacks;

        std::string FileName(const std::filesystem::path& path)
        {
            try {
                const auto utf8 = path.filename().u8string();
                return {utf8.begin(), utf8.end()};
            } catch (const std::filesystem::filesystem_error&) {
                return "<unreadable filename>";
            }
        }

        using Json = nlohmann::json;
        using Ids = std::unordered_set<std::string>;

        struct Invalid
        {
            std::string location;
            std::string reason;
        };

        void Require(bool condition, std::string_view location, std::string_view reason)
        {
            if (!condition) {
                throw Invalid{std::string(location), std::string(reason)};
            }
        }

        void Fields(const Json& value, const std::string& path,
            std::initializer_list<std::string_view> names,
            std::initializer_list<std::string_view> optional = {})
        {
            Require(value.is_object(), path, "Expected an object");
            for (const auto name : names) {
                Require(value.contains(name), path + "/" + std::string(name), "Missing required field");
            }
            for (const auto& [key, unused] : value.items()) {
                Require(std::find(names.begin(), names.end(), key) != names.end() ||
                    std::find(optional.begin(), optional.end(), key) != optional.end(), path,
                    "Unknown field: " + key);
            }
        }

        bool Whitespace(unsigned code)
        {
            return code == 0x20 || code == 0x85 || code == 0xA0 || code == 0x1680 ||
                (code >= 0x2000 && code <= 0x200A) || code == 0x2028 || code == 0x2029 ||
                code == 0x202F || code == 0x205F || code == 0x3000 || code == 0xFEFF;
        }

        std::string Text(const Json& value, const std::string& path, bool label = false)
        {
            Require(value.is_string(), path, "Expected a string");
            auto text = value.get<std::string>();
            if (!label) {
                return text;
            }
            // Display labels must contain visible Unicode text.
            unsigned code = 0;
            bool visible = false;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const auto byte = static_cast<unsigned char>(text[i]);
                if ((byte & 0xC0) == 0x80) {
                    code = (code << 6) | (byte & 0x3F);
                } else {
                    code = byte < 0x80 ? byte : byte & (byte < 0xE0 ? 0x1F : byte < 0xF0 ? 0x0F : 0x07);
                }
                if (i + 1 == text.size() || (static_cast<unsigned char>(text[i + 1]) & 0xC0) != 0x80) {
                    Require(code >= 0x20 && code != 0x7F, path, "Label contains a control character");
                    visible |= !Whitespace(code);
                }
            }
            Require(visible, path, "Label must contain visible text");
            return text;
        }

        bool Alpha(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        bool EventChar(char c)
        {
            return Alpha(c) || (c >= '0' && c <= '9') || c == '_';
        }

        std::string Id(const Json& value, const std::string& path, Ids& ids)
        {
            auto id = Text(value, path);
            Require(ValidId(id), path, "Expected a lowercase ID starting with a letter");
            Require(ids.insert(id).second, path, "Duplicate ID: " + id);
            return id;
        }

        const Json& Array(const Json& value, const std::string& path)
        {
            Require(value.is_array() && !value.empty(), path, "Expected a nonempty array");
            return value;
        }

        double Seconds(const Json& value, const std::string& path)
        {
            Require(value.is_number(), path, "Expected a positive number of seconds");
            const auto seconds = value.get<double>();
            Require(std::isfinite(seconds) && seconds > 0.0, path, "Expected a positive finite number of seconds");
            return seconds;
        }

        PlaybackOptions ParsePlayback(const Json& emote, const std::string& path, const std::string& scope)
        {
            const auto mode = Text(emote["mode"], path + "/mode");
            Require(mode == "once" || mode == "loop", path + "/mode",
                "Expected playback mode once or loop");
            PlaybackOptions options{mode == "once" ? PlaybackMode::Once : PlaybackMode::Loop};
            if (emote.contains("idle_form")) {
                const auto field = path + "/idle_form";
                const auto reference = Text(emote["idle_form"], field);
                const auto separator = reference.find('|');
                Require(separator != std::string::npos && separator > 0 &&
                    reference.find('|', separator + 1) == std::string::npos,
                    field, "Expected plugin|local hexadecimal FormID");
                const auto plugin = reference.substr(0, separator);
                Require(std::all_of(plugin.begin(), plugin.end(), [](unsigned char c) {
                    return c >= 0x20 && c != 0x7F && c != '/' && c != '\\' && c != ':';
                }), field, "Expected a plugin filename without a path or control characters");
                const auto hex = reference.substr(separator + 1);
                std::uint32_t id = 0;
                const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
                Require((hex.size() == 6 || hex.size() == 8) && parsed.ec == std::errc{} &&
                    parsed.ptr == hex.data() + hex.size() && id > 0 && id <= 0xFFFFFF,
                    field, "Expected a nonzero local FormID of six or eight hexadecimal digits");
                options.idle_form = IdleForm{plugin, id};
            }
            // A one-shot can have a completion timeout. Only loops can repeat an event.
            // Graph loops need neither field and must not be restarted every frame.
            if (emote.contains("duration_seconds")) {
                Require(mode == "once", path + "/duration_seconds", "Duration is only valid for once mode");
                options.duration_seconds = Seconds(emote["duration_seconds"], path + "/duration_seconds");
            }
            if (emote.contains("repeat_interval_seconds")) {
                Require(mode == "loop", path + "/repeat_interval_seconds", "Repeat interval is only valid for loop mode");
                options.repeat_interval_seconds = Seconds(emote["repeat_interval_seconds"], path + "/repeat_interval_seconds");
            }
            if (emote.contains("allow_movement")) {
                Require(emote["allow_movement"].is_boolean(), path + "/allow_movement", "Expected a Boolean");
                options.allow_movement = emote["allow_movement"].get<bool>();
            }
            options.state_scope = scope;
            if (emote.contains("state")) {
                options.state = Text(emote["state"], path + "/state", true);
                Require(mode == "loop", path + "/state", "A held state requires loop mode");
            }
            if (emote.contains("requires_state")) {
                options.requires_state = Text(emote["requires_state"], path + "/requires_state", true);
            }
            if (emote.contains("entry_seconds")) {
                Require(!options.state.empty(), path + "/entry_seconds", "Entry timing requires a held state");
                options.entry_seconds = Seconds(emote["entry_seconds"], path + "/entry_seconds");
            }
            if (emote.contains("state_only")) {
                Require(emote["state_only"].is_boolean(), path + "/state_only", "Expected a Boolean");
                options.state_only = emote["state_only"].get<bool>();
                Require(!options.state_only || !options.state.empty(), path + "/state_only",
                    "A state-only action requires a held state");
            }
            if (emote.contains("setup")) {
                Require(emote["setup"].is_array(), path + "/setup", "Expected an array");
                for (std::size_t i = 0; i < emote["setup"].size(); ++i) {
                    const auto stepPath = path + "/setup/" + std::to_string(i);
                    const auto& step = emote["setup"][i];
                    Fields(step, stepPath, {"event", "seconds"}, {"state_after"});
                    auto stepEvent = Text(step["event"], stepPath + "/event");
                    Require(!stepEvent.empty() && std::all_of(stepEvent.begin(), stepEvent.end(), EventChar),
                        stepPath + "/event", "Event must contain ASCII letters, digits or underscores");
                    options.setup.push_back({std::move(stepEvent), Seconds(step["seconds"], stepPath + "/seconds"),
                        step.contains("state_after") ? Text(step["state_after"], stepPath + "/state_after", true) : ""});
                }
            }
            // Authored quickstart must reach the required state. Without setup, an action
            // applies only to an already held/entering state and otherwise does nothing.
            Require(options.requires_state.empty() || options.state.empty() ||
                options.setup.empty() || options.setup.back().state_after == options.requires_state,
                path + "/setup", "Setup must reach the required state");
            Require(options.requires_state.empty() || !options.state.empty() || mode == "once",
                path + "/mode", "State exits require once mode");
            return options;
        }

        Emote ParseEmote(const Json& emote, const std::string& path, const std::string& scope, Ids& ids)
        {
            if (emote.contains("action")) {
                // Stop uses global cleanup without an animation event.
                Fields(emote, path, {"id", "name", "action"});
                Require(Text(emote["action"], path + "/action") == "stop",
                    path + "/action", "Expected stop action");
                return {Id(emote["id"], path + "/id", ids),
                    Text(emote["name"], path + "/name", true), {}, {}, {}, EmoteAction::Stop};
            }
            Fields(emote, path, {"id", "name", "event", "mode"},
                {"duration_seconds", "repeat_interval_seconds", "state", "requires_state",
                    "entry_seconds", "state_only", "setup", "variant_of", "allow_movement", "hidden", "idle_form"});
            auto event = Text(emote["event"], path + "/event");
            Require(!event.empty() &&
                std::all_of(event.begin(), event.end(), EventChar), path + "/event",
                "Event must contain ASCII letters, digits or underscores");
            if (emote.contains("variant_of")) {
                Require(emote["variant_of"].is_string() && ValidId(emote["variant_of"].get<std::string>()),
                    path + "/variant_of", "Expected a primary emote ID");
            }
            if (emote.contains("hidden")) {
                Require(emote["hidden"].is_boolean(), path + "/hidden", "Expected a Boolean");
            }
            const auto options = ParsePlayback(emote, path, scope);
            return {Id(emote["id"], path + "/id", ids),
                Text(emote["name"], path + "/name", true), std::move(event),
                options, emote.contains("variant_of") ? Text(emote["variant_of"], path + "/variant_of") : "",
                EmoteAction::Play, emote.value("hidden", false)};
        }

        void ValidateVariants(const Section& group, const std::string& path)
        {
            // Variants stay with their primary row. Chains and cross-section links
            // would make ordering and picker ownership ambiguous.
            for (const auto& emote : group.emotes) {
                if (emote.variant_of.empty()) continue;
                const auto parent = std::find_if(group.emotes.begin(), group.emotes.end(), [&](const auto& item) {
                    return item.id == emote.variant_of;
                });
                Require(parent != group.emotes.end() && parent->id != emote.id && parent->variant_of.empty() && parent->action == EmoteAction::Play &&
                    (emote.hidden || !parent->hidden),
                    path + "/emotes", "Variant must name a primary in the same section. Visible variants need a visible primary");
            }
        }

    }

    bool ValidId(std::string_view value)
    {
        return !value.empty() && value.front() >= 'a' && value.front() <= 'z' &&
            std::all_of(value.begin(), value.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            });
    }

    EmoteLocation FindEmote(const PackCatalogue& catalogue, const EmoteKey& key)
    {
        for (const auto& pack : catalogue.packs) {
            if (pack.id != key.pack) {
                continue;
            }
            const auto alias = pack.aliases.find(key.emote);
            const auto& id = alias == pack.aliases.end() ? key.emote : alias->second;
            for (const auto& category : pack.categories) {
                for (const auto& section : category.sections) {
                    for (const auto& emote : section.emotes) {
                        if (emote.id == id) {
                            return {&pack, &category, &section, &emote};
                        }
                    }
                }
            }
        }
        return {};
    }

    std::expected<Pack, PackError> ParsePack(std::string_view file, std::string_view text)
    {
        try {
            // Track keys separately for each open object so duplicate keys are reported
            // before the JSON library replaces an earlier value with a later one.
            std::vector<Ids> keys;
            const auto json = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
                // Setup adds a step array/object below the emote, giving nine container levels.
                Require(depth <= 9, "", "JSON nesting exceeds the pack schema");
                if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start) {
                    Require(depth < 9, "", "JSON nesting exceeds the pack schema");
                }
                if (event == Json::parse_event_t::object_start) {
                    keys.emplace_back();
                } else if (event == Json::parse_event_t::object_end) {
                    keys.pop_back();
                } else if (event == Json::parse_event_t::key) {
                    const auto key = value.get<std::string>();
                    Require(keys.back().insert(key).second, "", "Duplicate JSON key: " + key);
                }
                return true;
            });

            Fields(json, "", {"schema", "id", "name", "categories"}, {"aliases", "required_files", "graph"});
            Require(json["schema"].is_number() && json["schema"] == 1, "/schema",
                "Unsupported schema version; expected integer 1");
            // Category and emote IDs must be unique throughout this pack.
            // Duplicate pack IDs across different files are checked by LoadPacks.
            Ids packIds, categoryIds, emoteIds;
            // Build locally so an invalid entry rejects the whole pack.
            Pack pack{Id(json["id"], "/id", packIds), Text(json["name"], "/name", true), {}};
            const auto& categories = Array(json["categories"], "/categories");

            // Keep manifest order for menu tabs and buttons.
            for (std::size_t c = 0; c < categories.size(); ++c) {
                const auto path = "/categories/" + std::to_string(c);
                const auto& category = categories[c];
                Fields(category, path, {"id", "name", "sections"});
                Category output{Id(category["id"], path + "/id", categoryIds),
                    Text(category["name"], path + "/name", true), {}};
                // Each category gets its own set of section IDs. The same section ID
                // can appear in another category, but cannot repeat inside this one.
                Ids sectionIds;
                const auto& sections = Array(category["sections"], path + "/sections");
                for (std::size_t s = 0; s < sections.size(); ++s) {
                    const auto sectionPath = path + "/sections/" + std::to_string(s);
                    const auto& section = sections[s];
                    Fields(section, sectionPath, {"id", "name", "emotes"});
                    Section group{Id(section["id"], sectionPath + "/id", sectionIds),
                        Text(section["name"], sectionPath + "/name", true), {}};
                    // Event names are identifiers. Skyrim resolves them during playback.
                    const auto& emotes = Array(section["emotes"], sectionPath + "/emotes");
                    for (std::size_t e = 0; e < emotes.size(); ++e) {
                        const auto emotePath = sectionPath + "/emotes/" + std::to_string(e);
                        const auto& emote = emotes[e];
                        group.emotes.push_back(ParseEmote(emote, emotePath, pack.id, emoteIds));
                    }
                    ValidateVariants(group, sectionPath);
                    output.sections.push_back(std::move(group));
                }
                pack.categories.push_back(std::move(output));
            }
            if (json.contains("aliases")) {
                Require(json["aliases"].is_object(), "/aliases", "Expected an object");
                for (const auto& [alias, target] : json["aliases"].items()) {
                    const auto path = "/aliases/" + alias;
                    Require(ValidId(alias) && !emoteIds.contains(alias), path,
                        "Alias must be a valid ID distinct from current emote IDs");
                    const auto id = Text(target, path);
                    Require(emoteIds.contains(id), path, "Alias must name a current emote ID");
                    pack.aliases.emplace(alias, id);
                }
            }
            if (json.contains("required_files")) {
                Require(json["required_files"].is_array(), "/required_files", "Expected an array");
                for (std::size_t i = 0; i < json["required_files"].size(); ++i) {
                    const auto path = "/required_files/" + std::to_string(i);
                    const auto name = Text(json["required_files"][i], path, true);
                    Require(name.find_first_of("\\:*?\"<>|") == std::string::npos, path,
                        "Use a relative Data path with forward slashes");
                    std::size_t offset = 0;
                    for (;;) {
                        const auto slash = name.find('/', offset);
                        const auto part = name.substr(offset, slash == std::string::npos ? slash : slash - offset);
                        Require(!part.empty() && part != "." && part != ".." && part.back() != '.' && part.back() != ' ',
                            path, "Path cannot contain empty, dot or parent components or trailing dots/spaces");
                        if (slash == std::string::npos) break;
                        offset = slash + 1;
                    }
                    pack.required_files.push_back(name);
                }
            }
            if (json.contains("graph")) {
                const auto& graph = json["graph"];
                Fields(graph, "/graph", {"variable", "minimum"});
                const auto variable = Text(graph["variable"], "/graph/variable");
                Require(!variable.empty() && std::all_of(variable.begin(), variable.end(), EventChar),
                    "/graph/variable", "Expected an ASCII graph variable name");
                Require(graph["minimum"].is_number_integer() && graph["minimum"] >= 0 &&
                    graph["minimum"] <= std::numeric_limits<std::int32_t>::max(),
                    "/graph/minimum", "Expected a nonnegative 32-bit integer revision");
                pack.graph = GraphRequirement{variable, graph["minimum"].get<std::int32_t>()};
            }
            return pack;
        } catch (const Invalid& error) {
            return std::unexpected(PackError{std::string(file), error.location, error.reason});
        } catch (const Json::parse_error& error) {
            return std::unexpected(PackError{std::string(file), "byte " + std::to_string(error.byte),
                "Invalid JSON syntax or UTF-8"});
        } catch (const Json::out_of_range&) {
            return std::unexpected(PackError{std::string(file), "", "JSON number is outside the supported range"});
        }
    }

    std::vector<std::string> CheckPackFiles(const Pack& pack, const std::filesystem::path& data)
    {
        std::vector<std::string> warnings;
        for (const auto& name : pack.required_files) {
            std::error_code ec;
            // Mod managers may deploy symlinks. Check availability without rejecting those paths.
            if (!std::filesystem::is_regular_file(data / std::filesystem::path(std::u8string(name.begin(), name.end())), ec)) {
                warnings.push_back("Missing or unreadable Data/" + name + ". Reinstall or deploy this pack.");
            }
        }
        return warnings;
    }

    std::string CheckGraphRevision(const GraphRequirement& requirement, bool inspected,
        std::optional<std::int32_t> revision)
    {
        if (!inspected) return "Graph readiness could not be checked. Local playback remains available.";
        if (revision && *revision >= requirement.minimum) return {};
        const auto state = revision ? " is " + std::to_string(*revision) : " is missing";
        return "Loaded graph marker " + requirement.variable + state + ", needs " + std::to_string(requirement.minimum) +
            ". Deploy this pack, regenerate behaviors and restart Skyrim. Local playback remains available.";
    }

    PackCatalogue LoadPacks(const std::filesystem::path& directory)
    {
        // A failure in one file should leave unrelated valid packs available.
        PackCatalogue catalogue;
        const auto error = [&](const std::filesystem::path& file, std::string reason) {
            catalogue.errors.push_back({FileName(file), "", std::move(reason)});
        };
        // Directory failures return an error instead of an incomplete catalogue.
        std::error_code ec;
        std::filesystem::directory_iterator entry(directory, ec), end;
        if (ec) {
            error(directory, "Cannot open pack directory");
            return catalogue;
        }
        std::vector<std::filesystem::path> files;
        for (; entry != end; entry.increment(ec)) {
            const auto extension = entry->path().extension().native();
            if (extension.size() != 5 || !std::equal(extension.begin(), extension.end(), ".json",
                    [](auto actual, char expected) {
                        return (actual >= 'A' && actual <= 'Z' ? actual + ('a' - 'A') : actual) == expected;
                    })) {
                continue;
            }
            std::error_code statusError;
            if (!entry->is_regular_file(statusError)) {
                if (statusError) {
                    error(entry->path(), "Cannot inspect pack file");
                }
                continue;
            }
            files.push_back(entry->path());
        }
        if (ec) {
            error(directory, "Cannot finish reading pack directory");
            return catalogue;
        }
        // Sort filenames so filesystem enumeration order cannot rearrange the menu.
        std::sort(files.begin(), files.end());

        std::vector<std::pair<std::string, Pack>> parsed;
        std::unordered_map<std::string, std::size_t> counts;
        for (const auto& file : files) {
            std::ifstream input(file, std::ios::binary);
            if (!input) {
                error(file, "Cannot open pack file");
                continue;
            }
            std::string text;
            std::array<char, 8192> buffer;
            while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {
                text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
            }
            if (input.bad() || (input.fail() && !input.eof())) {
                error(file, "Cannot read pack file");
                continue;
            }
            auto result = ParsePack(FileName(file), text);
            if (!result) {
                catalogue.errors.push_back(std::move(result.error()));
                continue;
            }
            ++counts[result->id];
            parsed.emplace_back(FileName(file), std::move(*result));
        }
        // Exclude every file involved in an ID conflict. Selecting the first file
        // would make saved favorites depend on which conflicting pack happened to win.
        for (auto& [file, pack] : parsed) {
            if (counts.at(pack.id) > 1) {
                catalogue.errors.push_back({file, "/id", "Duplicate pack ID: " + pack.id});
            } else {
                catalogue.packs.push_back(std::move(pack));
            }
        }
        return catalogue;
    }

    void ReloadPacks(const std::filesystem::path& directory)
    {
        loadedPacks = LoadPacks(directory);
    }

    const PackCatalogue& GetPacks()
    {
        return loadedPacks;
    }
}
