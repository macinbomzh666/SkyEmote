// SPDX-License-Identifier: MIT
#include "Favorites.h"
#include <json.hpp>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <Windows.h>

namespace SkyEmote
{
    namespace
    {
        struct FavoriteData
        {
            std::vector<EmoteKey> items;
            std::map<EmoteKey, unsigned> bindings;
        };

        FavoriteData ParseFavorites(std::string_view text)
        {
            std::vector<std::set<std::string>> keys;
            const auto json = nlohmann::json::parse(text, [&](int depth, auto event, auto& value) {
                using Event = nlohmann::json::parse_event_t;
                // Favorites contain a root object, an array and flat key objects.
                if (depth > 3 || ((event == Event::object_start || event == Event::array_start) && depth >= 3)) {
                    throw std::runtime_error("Favorites nesting is invalid");
                }
                if (event == Event::object_start) keys.emplace_back();
                if (event == Event::object_end) keys.pop_back();
                if (event == Event::key && !keys.back().insert(value.template get<std::string>()).second) {
                    throw std::runtime_error("Duplicate favorites field");
                }
                return true;
            });
            if (!json.is_object() || json.size() != 2 || !json.contains("schema") ||
                json["schema"] != 1 || !json.contains("favorites") || !json["favorites"].is_array()) {
                throw std::runtime_error("Unsupported favorites format");
            }
            std::vector<EmoteKey> loaded;
            std::set<EmoteKey> seen;
            std::map<EmoteKey, unsigned> bindings;
            std::set<unsigned> used_hotkeys;
            for (const auto& item : json["favorites"]) {
                if (!item.is_object() || item.size() != (item.contains("hotkey") ? 3 : 2) || !item.contains("pack") ||
                    !item.contains("emote") || !item["pack"].is_string() || !item["emote"].is_string()) {
                    throw std::runtime_error("Invalid favorite key");
                }
                EmoteKey key{item["pack"].get<std::string>(), item["emote"].get<std::string>()};
                if (!ValidId(key.pack) || !ValidId(key.emote)) {
                    throw std::runtime_error("Invalid favorite ID");
                }
                unsigned hotkey = 0;
                if (item.contains("hotkey")) {
                    if (!item["hotkey"].is_number_integer() || item["hotkey"] < 2 || item["hotkey"] > 255) {
                        throw std::runtime_error("Invalid favorite hotkey");
                    }
                    hotkey = item["hotkey"].get<unsigned>();
                }
                if (seen.insert(key).second) {
                    if (hotkey && !used_hotkeys.insert(hotkey).second) {
                        throw std::runtime_error("Duplicate favorite hotkey");
                    }
                    loaded.push_back(key);
                    if (hotkey) bindings[key] = hotkey;
                }
            }
            return {std::move(loaded), std::move(bindings)};
        }
    }

    void Favorites::Load(const std::filesystem::path& path, const std::filesystem::path& legacy)
    {
        file = path;
        items.clear();
        hotkeys.clear();
        error.clear();
        writable = false;
        if (file.empty()) {
            error = "Cannot locate the user directory for favorites.";
            return;
        }
        auto source = file;
        std::string operation = "load favorites";
        try {
            if (!std::filesystem::exists(file)) {
                if (legacy.empty() || !std::filesystem::exists(legacy)) {
                    writable = Save({}, {});
                    return;
                }
                source = legacy;
            }
            std::ifstream input(source, std::ios::binary);
            if (!input) {
                throw std::runtime_error("Cannot open favorites");
            }
            const std::string text{std::istreambuf_iterator<char>(input), {}};
            if (input.bad()) {
                throw std::runtime_error("Cannot read favorites");
            }
            input.close();
            auto loaded = ParseFavorites(text);
            // Keep validated data usable even if migration cannot finish. Edits stay
            // disabled until both the destination and legacy cleanup succeed.
            items = std::move(loaded.items);
            hotkeys = std::move(loaded.bindings);
            // Copy a validated legacy file before removing its source, including across drives.
            // A valid current file takes precedence. Neither case leaves a legacy backup.
            if (source != file) {
                operation = "copy favorites to " + file.string();
                if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
                std::filesystem::copy_file(source, file, std::filesystem::copy_options::none);
            }
            if (!legacy.empty() && legacy != file && std::filesystem::exists(legacy)) {
                operation = "remove legacy favorites at " + legacy.string();
                std::filesystem::remove(legacy);
            }
            writable = true;
        } catch (const std::exception& failure) {
            error = "Cannot " + operation + ": " + failure.what() + ". Favorites source: " + source.string() +
                ". Repair the file or migration destination and restart Skyrim.";
        }
    }

    bool Favorites::Toggle(const EmoteKey& key)
    {
        if (!writable || !ValidId(key.pack) || !ValidId(key.emote)) return false;
        auto candidate = items;
        const auto found = std::find(candidate.begin(), candidate.end(), key);
        const bool removing = found != candidate.end();
        if (removing) candidate.erase(found);
        else candidate.push_back(key);
        auto bindings = hotkeys;
        if (removing) bindings.erase(key);
        return Save(candidate, bindings);
    }

    unsigned Favorites::Hotkey(const EmoteKey& key) const
    {
        const auto found = hotkeys.find(key);
        return found == hotkeys.end() ? 0 : found->second;
    }

    std::optional<EmoteKey> Favorites::FindHotkey(unsigned scan_code) const
    {
        for (const auto& [key, code] : hotkeys) if (code == scan_code) return key;
        return {};
    }

    bool Favorites::BindHotkey(const EmoteKey& key, unsigned scan_code)
    {
        if (!writable || scan_code == 1 || scan_code > 255 ||
            std::find(items.begin(), items.end(), key) == items.end()) return false;
        auto bindings = hotkeys;
        // Assigning an occupied key moves it to this favorite. One press can play only one emote.
        std::erase_if(bindings, [&](const auto& entry) { return entry.first == key || entry.second == scan_code; });
        if (scan_code) bindings[key] = scan_code;
        return Save(items, bindings);
    }

    bool Favorites::Save(const std::vector<EmoteKey>& candidate, const std::map<EmoteKey, unsigned>& bindings)
    {
        nlohmann::json json{{"schema", 1}, {"favorites", nlohmann::json::array()}};
        for (const auto& item : candidate) {
            nlohmann::json entry{{"pack", item.pack}, {"emote", item.emote}};
            if (const auto binding = bindings.find(item); binding != bindings.end()) entry["hotkey"] = binding->second;
            json["favorites"].push_back(entry);
        }
        auto temporary = file;
        temporary += ".tmp";
        std::error_code ec;
        if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
        if (!ec) {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            const auto text = json.dump(2) + "\n";
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            if (output.fail()) ec = std::make_error_code(std::errc::io_error);
        }
        // Replace only after a complete write. A failed save keeps the previous file and UI selection.
        if (!ec) {
            if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            }
        }
        if (ec) {
            error = "Cannot save favorites: " + ec.message();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        items = candidate;
        hotkeys = bindings;
        error.clear();
        return true;
    }
    std::map<unsigned, std::optional<Emote>> Favorites::ResolveHotkeys(const PackCatalogue& catalogue) const
    {
        std::map<unsigned, std::optional<Emote>> resolved;
        for (const auto& [key, code] : hotkeys) {
            if (!code) continue;
            const auto found = FindEmote(catalogue, key);
            resolved.emplace(code, found ? std::optional<Emote>(*found.emote) : std::nullopt);
        }
        return resolved;
    }

}
