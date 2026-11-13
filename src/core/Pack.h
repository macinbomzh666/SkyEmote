// SPDX-License-Identifier: MIT
#pragma once

#include <expected>
#include <compare>
#include <map>
#include <optional>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SkyEmote
{
    enum class PlaybackMode { Once, Loop };

    struct IdleForm
    {
        std::string plugin;
        std::uint32_t id;
    };

    struct PlaybackStep
    {
        std::string event;
        double seconds;
        std::string state_after;
    };

    struct PlaybackOptions
    {
        PlaybackMode mode = PlaybackMode::Once;
        // Authored walking gestures produce graph movement without a gameplay movement request.
        bool allow_movement = false;
        // Zero relies on the graph's IdleStop completion event instead of a fallback timeout.
        double duration_seconds = 0.0;
        // Zero lets the graph loop itself. A positive interval repeats a native one-shot event.
        double repeat_interval_seconds = 0.0;
        // Filled from the pack ID by the loader. Equal labels in different packs are unrelated states.
        std::string state_scope;
        std::string state;
        std::string requires_state;
        // Commit state only after this entry time. Zero commits it when the main event starts.
        double entry_seconds = 0.0;
        // Selecting an already achieved state should not replay its entry animation.
        bool state_only = false;
        std::vector<PlaybackStep> setup;
        // Native IDLE forms establish engine action context that a bare graph event can lack.
        std::optional<IdleForm> idle_form;
    };

    enum class EmoteAction { Play, Stop };

    struct Emote
    {
        std::string id;
        std::string name;
        std::string event;
        PlaybackOptions playback;
        std::string variant_of;
        EmoteAction action = EmoteAction::Play;
        // Retain stable IDs for existing favorites without offering redundant browser choices.
        bool hidden = false;
    };

    struct Section
    {
        std::string id;
        std::string name;
        std::vector<Emote> emotes;
    };

    struct Category
    {
        std::string id;
        std::string name;
        std::vector<Section> sections;
    };

    struct GraphRequirement
    {
        std::string variable;
        std::int32_t minimum;
    };

    struct Pack
    {
        std::string id;
        std::string name;
        std::vector<Category> categories;
        std::map<std::string, std::string> aliases;
        std::vector<std::string> required_files;
        std::optional<GraphRequirement> graph;
    };

    struct PackError
    {
        std::string file;
        std::string location;
        std::string reason;
    };

    struct PackCatalogue
    {
        std::vector<Pack> packs;
        std::vector<PackError> errors;
    };

    struct EmoteKey
    {
        std::string pack;
        std::string emote;
        auto operator<=>(const EmoteKey&) const = default;
    };

    struct EmoteLocation
    {
        const Pack* pack{};
        const Category* category{};
        const Section* section{};
        const Emote* emote{};
        explicit operator bool() const { return emote != nullptr; }
    };

    bool ValidId(std::string_view value);
    std::vector<std::string> CheckPackFiles(const Pack& pack, const std::filesystem::path& data);
    std::string CheckGraphRevision(const GraphRequirement& requirement, bool inspected,
        std::optional<std::int32_t> revision);
    EmoteLocation FindEmote(const PackCatalogue& catalogue, const EmoteKey& key);

    // Locations are JSON pointers, or a byte offset for JSON syntax errors.
    std::expected<Pack, PackError> ParsePack(std::string_view file, std::string_view text);
    PackCatalogue LoadPacks(const std::filesystem::path& directory);
    void ReloadPacks(const std::filesystem::path& directory);
    const PackCatalogue& GetPacks();
}
