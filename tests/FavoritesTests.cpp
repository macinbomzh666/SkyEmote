// SPDX-License-Identifier: MIT
#include "Favorites.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <Windows.h>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("SkyEmote-favorites-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Directory() { std::filesystem::create_directory(path); }
        ~Directory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    };
}
int main()
{
    try {
        Directory directory;
        const auto path = directory.path / "user/favorites.json";
        SkyEmote::Favorites favorites;
        // An unavailable user directory must not write into Skyrim's working directory.
        favorites.Load({});
        Check(!favorites.Error().empty() && !favorites.Toggle({"vanilla", "wave"}), "Empty storage path accepted");
        // Reinstalls and catalogue changes do not affect the user's saved keys.
        favorites.Load(path);
        Check(favorites.Error().empty() && std::filesystem::exists(path), "Missing file should create defaults");
        SkyEmote::Favorites defaults;
        defaults.Load(path);
        Check(defaults.Error().empty() && defaults.Items().empty(), "Generated defaults are not a valid empty catalogue");
        Check(favorites.Toggle({"absent-pack", "wave"}), "First save failed");
        Check(favorites.Toggle({"vanilla", "bow"}), "Replacement save failed");
        SkyEmote::Favorites restored;
        restored.Load(path);
        Check(restored.Items().size() == 2, "Round trip lost a missing pack");
        Check(restored.Toggle({"absent-pack", "wave"}), "Cannot remove missing favorite");
        favorites.Load(path);
        Check(favorites.Items().size() == 1 && favorites.Items()[0].emote == "bow", "Removal was not saved");
        // Hotkeys survive reloads, move between favorites and disappear on removal.
        Check(favorites.BindHotkey({"vanilla", "bow"}, 2), "Cannot bind favorite");
        restored.Load(path);
        Check(restored.Hotkey({"vanilla", "bow"}) == 2 &&
            restored.FindHotkey(2) == SkyEmote::EmoteKey{"vanilla", "bow"}, "Hotkey round trip failed");
        Check(restored.Toggle({"vanilla", "wave"}) && restored.BindHotkey({"vanilla", "wave"}, 2), "Cannot move binding");
        Check(restored.Hotkey({"vanilla", "bow"}) == 0 && restored.Hotkey({"vanilla", "wave"}) == 2,
            "One key still plays two favorites");
        // Quick Emote snapshots resolve aliases and copy playback data for the input
        // thread. An unavailable favorite keeps its binding without inventing an emote.
        SkyEmote::PackCatalogue quick_catalogue;
        SkyEmote::Pack pack;
        pack.id = "vanilla";
        pack.aliases.emplace("wave", "renamed-wave");
        SkyEmote::Emote wave;
        wave.id = "renamed-wave";
        wave.event = "IdleWave";
        wave.playback.state_scope = "vanilla";
        SkyEmote::Section section;
        section.emotes.push_back(wave);
        SkyEmote::Category category;
        category.sections.push_back(section);
        pack.categories.push_back(category);
        quick_catalogue.packs.push_back(pack);
        const auto bindings = restored.ResolveHotkeys(quick_catalogue);
        Check(bindings.size() == 1 && bindings.at(2) && bindings.at(2)->id == "renamed-wave" &&
            bindings.at(2)->playback.state_scope == "vanilla", "Quick Emote lost the binding or alias playback data");
        quick_catalogue.packs[0].categories[0].sections[0].emotes[0].event = "Changed";
        Check(bindings.at(2)->event == "IdleWave", "Quick Emote snapshot retained mutable catalogue data");
        const auto unavailable = restored.ResolveHotkeys({});
        Check(unavailable.size() == 1 && !unavailable.at(2), "Unavailable favorite binding was lost");
        Check(!restored.BindHotkey({"vanilla", "wave"}, 1) && !restored.BindHotkey({"vanilla", "wave"}, 256) &&
            !restored.BindHotkey({"vanilla", "missing"}, 3), "Invalid binding accepted");
        Check(restored.BindHotkey({"vanilla", "wave"}, 0) && !restored.FindHotkey(2), "Cannot clear binding");
        Check(restored.BindHotkey({"vanilla", "wave"}, 3) && restored.Toggle({"vanilla", "wave"}) &&
            !restored.FindHotkey(3), "Removed favorite kept a binding");
        restored.Load(path);
        Check(restored.Items().size() == 1 && restored.Hotkey({"vanilla", "bow"}) == 0, "Unbinding was not saved");
        // A corrupt file is never overwritten by a button click.
        { std::ofstream output(path); output << "{broken"; }
        favorites.Load(path);
        Check(!favorites.Error().empty() && favorites.Items().empty(), "Corruption was hidden");
        Check(!favorites.Toggle({"vanilla", "wave"}), "Corrupt file overwritten");
        std::ifstream input(path);
        std::string text; input >> text;
        Check(text == "{broken", "Corrupt source changed");
        // Duplicate keys are rejected, while duplicate entries collapse in original order.
        { std::ofstream output(path); output << R"({"schema":1,"schema":1,"favorites":[]})"; }
        favorites.Load(path);
        Check(!favorites.Error().empty(), "Duplicate JSON keys accepted");
        { std::ofstream output(path); output << R"({"schema":1,"favorites":[{"pack":"vanilla","emote":"bow"},{"pack":"vanilla","emote":"bow"}]})"; }
        favorites.Load(path);
        Check(favorites.Items().size() == 1 && favorites.Error().empty(), "Duplicate entries not normalized");
        // Malformed or ambiguous bindings preserve the source file rather than losing favorites.
        for (const auto hotkey : {"0", "1", "-1", "256", "true", "2.5", "\"2\""}) {
            { std::ofstream output(path); output << "{\"schema\":1,\"favorites\":[{\"pack\":\"vanilla\",\"emote\":\"bow\",\"hotkey\":" << hotkey << "}]}"; }
            favorites.Load(path);
            Check(!favorites.Error().empty() && !favorites.BindHotkey({"vanilla", "bow"}, 2), "Invalid file binding accepted");
        }
        { std::ofstream output(path); output << R"({"schema":1,"favorites":[{"pack":"vanilla","emote":"bow","hotkey":2},{"pack":"vanilla","emote":"wave","hotkey":2}]})"; }
        favorites.Load(path);
        Check(!favorites.Error().empty(), "Ambiguous hotkey accepted");
        // A filesystem failure leaves the in-memory selection unchanged.
        const auto blocked = directory.path / "blocked";
        std::filesystem::create_directory(blocked);
        favorites.Load(blocked / "favorites.json");
        std::filesystem::remove(blocked / "favorites.json");
        std::filesystem::remove(blocked);
        { std::ofstream output(blocked); output << "file"; }
        Check(!favorites.Toggle({"vanilla", "bow"}) && favorites.Items().empty(), "Failed save changed selection");
        // A failed rebinding save keeps the previous key active in memory.
        const auto bindings_path = directory.path / "bindings/favorites.json";
        favorites.Load(bindings_path);
        Check(favorites.Toggle({"vanilla", "bow"}) && favorites.BindHotkey({"vanilla", "bow"}, 2), "Binding setup failed");
        std::filesystem::remove(bindings_path);
        std::filesystem::remove(bindings_path.parent_path());
        { std::ofstream output(bindings_path.parent_path()); output << "file"; }
        Check(!favorites.BindHotkey({"vanilla", "bow"}, 3) && favorites.Hotkey({"vanilla", "bow"}) == 2,
            "Failed save replaced the active binding");
        // Move validated favorites beside the INI and remove the old location only after success.
        const auto legacy = directory.path / "legacy-favorites.json";
        const auto current = directory.path / "plugins/SkyEmote.favorites.json";
        favorites.Load(legacy);
        Check(favorites.Toggle({"vanilla", "bow"}) && favorites.BindHotkey({"vanilla", "bow"}, 2), "Migration setup failed");
        restored.Load(current, legacy);
        Check(restored.Error().empty() && restored.Hotkey({"vanilla", "bow"}) == 2 &&
            !std::filesystem::exists(legacy) && std::filesystem::exists(current), "Migration lost data or kept a backup");
        Check(restored.Toggle({"vanilla", "bow"}), "Cannot edit migrated favorites");
        { std::ofstream output(legacy); output << "stale old file"; }
        restored.Load(current, legacy);
        Check(restored.Error().empty() && restored.Items().empty() && !std::filesystem::exists(legacy),
            "Current favorites were overwritten or the stale old file remained");
        const auto corrupt_legacy = directory.path / "corrupt-legacy.json";
        { std::ofstream output(corrupt_legacy); output << "{broken"; }
        const auto missing = directory.path / "no-migration.json";
        restored.Load(missing, corrupt_legacy);
        Check(!restored.Error().empty() && !std::filesystem::exists(missing) &&
            !restored.Toggle({"vanilla", "wave"}), "Corrupt legacy source migrated or became writable");
        favorites.Load(legacy);
        restored.Load(blocked / "migration.json", legacy);
        Check(!restored.Error().empty() && !restored.Toggle({"vanilla", "wave"}) &&
            std::filesystem::exists(legacy), "Failed migration deleted its source or became writable");
        // Validated favorites remain playable when copying or legacy cleanup fails.
        Check(favorites.Toggle({"vanilla", "bow"}) && favorites.BindHotkey({"vanilla", "bow"}, 2),
            "Read-only migration setup failed");
        restored.Load(blocked / "migration.json", legacy);
        Check(restored.Items().size() == 1 && restored.Hotkey({"vanilla", "bow"}) == 2 &&
            !restored.BindHotkey({"vanilla", "bow"}, 3), "Failed copy hid valid data or enabled editing");
        const auto cleanup_directory = directory.path / "legacy-directory";
        std::filesystem::create_directory(cleanup_directory);
        { std::ofstream output(cleanup_directory / "keep"); output << "preserve"; }
        restored.Load(legacy, cleanup_directory);
        Check(!restored.Error().empty() && restored.Items().size() == 1 &&
            restored.Hotkey({"vanilla", "bow"}) == 2 && !restored.Toggle({"vanilla", "wave"}) &&
            std::filesystem::exists(cleanup_directory / "keep"), "Failed cleanup hid current data or enabled editing");
        // Windows permits reading/copying this source but denies deletion while held.
        const auto locked = CreateFileW(legacy.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(locked != INVALID_HANDLE_VALUE, "Cannot hold the migration source open");
        const auto copied = directory.path / "copied-favorites.json";
        restored.Load(copied, legacy);
        CloseHandle(locked);
        Check(std::filesystem::exists(copied) && std::filesystem::exists(legacy) &&
            !restored.Error().empty() && restored.Hotkey({"vanilla", "bow"}) == 2 &&
            !restored.BindHotkey({"vanilla", "bow"}, 3), "Failed post-copy deletion lost data or enabled editing");
        restored.Load(copied, legacy);
        Check(restored.Error().empty() && !std::filesystem::exists(legacy) &&
            restored.BindHotkey({"vanilla", "bow"}, 3), "Repaired migration did not restore editing");
        // A damaged current file still wins source selection. A readable legacy
        // file must not silently replace it or be removed during that failure.
        { std::ofstream output(legacy); output << R"({"schema":1,"favorites":[{"pack":"vanilla","emote":"bow","hotkey":2}]})"; }
        { std::ofstream output(current); output << "{broken"; }
        restored.Load(current, legacy);
        Check(!restored.Error().empty() && restored.Items().empty() &&
            std::filesystem::exists(legacy) && !restored.Toggle({"vanilla", "wave"}),
            "Malformed current data fell back to legacy favorites or enabled editing");
        // Renamed emotes resolve only through validated, direct aliases.
        const std::string manifest = R"({"schema":1,"id":"demo","name":"Demo","aliases":{"old":"wave"},"categories":[{"id":"social","name":"Social","sections":[{"id":"greetings","name":"Greetings","emotes":[{"id":"wave","name":"Wave","event":"IdleWave","mode":"once"}]}]}]})";
        auto parsed = SkyEmote::ParsePack("demo.json", manifest);
        Check(parsed.has_value(), "Alias rejected");
        SkyEmote::PackCatalogue catalogue{{*parsed}, {}};
        Check(SkyEmote::FindEmote(catalogue, {"demo", "old"}).emote->id == "wave", "Alias resolution failed");
        Check(!SkyEmote::FindEmote(catalogue, {"missing", "old"}), "Missing pack resolved");
        // A shortcut keeps the saved alias while resolving the current animation at playback.
        favorites.Load(directory.path / "alias-favorites.json");
        Check(favorites.Toggle({"demo", "old"}) && favorites.BindHotkey({"demo", "old"}, 2), "Alias binding failed");
        restored.Load(directory.path / "alias-favorites.json");
        const auto alias_key = restored.FindHotkey(2);
        Check(alias_key && SkyEmote::FindEmote(catalogue, *alias_key).emote->id == "wave", "Bound alias did not resolve");
        auto bad = manifest;
        bad.replace(bad.find("\"old\":\"wave\""), 12, "\"old\":\"old\"");
        Check(!SkyEmote::ParsePack("bad.json", bad), "Alias cycle accepted");
        std::cout << "Favorites checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
