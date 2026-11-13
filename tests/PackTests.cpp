// SPDX-License-Identifier: MIT
#include "Pack.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <json.hpp>

namespace
{
    using Json = nlohmann::json;
    int checks = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
        ++checks;
    }

    SkyEmote::Pack Accept(const std::string& text)
    {
        auto result = SkyEmote::ParsePack("example.json", text);
        if (!result) {
            throw std::runtime_error(result.error().location + ": " + result.error().reason);
        }
        ++checks;
        return std::move(*result);
    }

    void Reject(const std::string& text, std::string_view location, std::string_view reason = "")
    {
        const auto result = SkyEmote::ParsePack("broken.json", text);
        Check(!result, "Invalid input was accepted");
        Check(result.error().file == "broken.json", "Diagnostic lost its filename");
        Check(result.error().location == location, "Wrong diagnostic location: " + result.error().location);
        Check(!result.error().reason.empty() && result.error().reason.find(reason) != std::string::npos,
            "Wrong diagnostic reason: " + result.error().reason);
    }
}

int main(int argc, char** argv)
{
    try {
        // Parse the bundled examples and preserve their native playback classifications.
        Check(argc == 2, "Expected the bundled manifest path");
        std::ifstream input(argv[1], std::ios::binary);
        Check(input.is_open(), "Cannot open the bundled manifest");
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        const auto pack = Accept(text);
        Check(pack.id == "vanilla" && pack.name == "Vanilla", "Wrong pack identity");
        const auto find = [&](const std::string& id) -> const SkyEmote::Emote& {
            for (const auto& category : pack.categories) {
                for (const auto& section : category.sections) {
                    for (const auto& emote : section.emotes) if (emote.id == id) return emote;
                }
            }
            throw std::runtime_error("Missing native example: " + id);
        };
        const auto& wave = find("idlewave");
        Check(wave.id == "idlewave" && wave.name == "Wave" && wave.event == "IdleWave" &&
            wave.playback.mode == SkyEmote::PlaybackMode::Once && wave.playback.duration_seconds == 12.0,
            "Wrong Wave data");
        const auto& surrender = find("idlesurrender");
        Check(surrender.event == "IdleSurrender" && surrender.playback.mode == SkyEmote::PlaybackMode::Once &&
            surrender.playback.repeat_interval_seconds == 0.0, "Native Surrender must remain a one-shot");
        const auto& warm = find("idlewarmhands");
        Check(warm.playback.mode == SkyEmote::PlaybackMode::Loop && warm.playback.repeat_interval_seconds == 0.0,
            "Warm hands must use its graph loop without repeated starts");
        const auto& dance = find("idlecicerodance1");
        Check(dance.playback.mode == SkyEmote::PlaybackMode::Loop && dance.playback.repeat_interval_seconds > 6.7 &&
            dance.playback.repeat_interval_seconds < 6.8, "Cicero dance lost its native repeat timing");
        // Native repeats differ from graph-held poses and must retain their measured intervals.
        Check(find("idlewarmarms").playback.repeat_interval_seconds > 3.2 &&
            find("idlewarmarms").playback.repeat_interval_seconds < 3.3,
            "Warm arms lost its repeat timing");
        Check(find("idlecicerodance2").playback.repeat_interval_seconds == 6.08 &&
            find("idlecicerodance3").playback.repeat_interval_seconds > 2.4,
            "Distinct dances lost their native periods");
        Check(find("idlechildcryingstart").playback.state == "Cry" &&
            find("idledrumstart").playback.repeat_interval_seconds == 0.0,
            "Held poses should not restart like one-shot repeats");
        // Native quickstart and manual entry must agree on the state and its entry duration.
        const auto& read = find("idlebook_reading").playback;
        const auto& page = find("idlebook_turnonepage").playback;
        Check(read.state_only && read.state == "Reading" && page.requires_state == read.state &&
            page.setup[0].seconds == read.entry_seconds, "Book entry and quickstart disagree");
        const auto& toast = find("idlemq201toaststart").playback;
        Check(toast.requires_state == "Drinking" && toast.setup[0].state_after == toast.requires_state,
            "Toast cannot quickstart into its parent state");
        const auto& floatAction = find("idlenocturnal_idlea").playback;
        Check(floatAction.setup.size() == 2 && floatAction.setup[0].state_after == "Floating" &&
            floatAction.setup[1].state_after == floatAction.requires_state, "Floating lost its two-stage entry");
        const auto& floatExit = find("idlenocturnal_exit").playback;
        Check(floatExit.mode == SkyEmote::PlaybackMode::Loop && floatExit.state == "Floating (lowered)" &&
            floatExit.requires_state == "Floating (raised)" && floatExit.setup.empty(),
            "Unrise should retain lower floating without quickstarting or resetting to standing");

        // Ledger authoring needs its entry before writing, while independent poses enter once.
        const auto& ledger = find("idlewriteledgerenter").playback;
        Check(ledger.setup.size() == 1 && ledger.setup[0].event == "IdleWriteLedgerEnter" &&
            find("idlewriteledgerenter").event == "IdleWriteLedgerWrite", "Ledger lost its two-step sequence");
        Check(find("idlesitcrossleggedenter").playback.mode == SkyEmote::PlaybackMode::Loop &&
            find("idlelaydownenter").playback.repeat_interval_seconds == 0.0,
            "Position entry must settle into a pose without repeated starts");

        // Moving dialogue must keep authored movement, not enable gameplay movement controls.
        Check(find("idledialoguemovingtalka").playback.allow_movement &&
            !find("idlewave").playback.allow_movement &&
            find("idledialoguemovingtalkd").playback.duration_seconds == 10.76667,
            "Moving dialogue classification or safe completion timing changed");

        // Variant ownership is local to a section and cannot form chains or cycles.
        auto variants = Json::parse(text);
        auto& family = variants["categories"][0]["sections"][0]["emotes"];
        family[1]["variant_of"] = family[0]["id"];
        Accept(variants.dump());
        family[1]["variant_of"] = family[1]["id"];
        Reject(variants.dump(), "/categories/0/sections/0/emotes");
        family[1]["variant_of"] = "";
        Reject(variants.dump(), "/categories/0/sections/0/emotes/1/variant_of");
        family[1]["variant_of"] = "idlesurrender";
        Reject(variants.dump(), "/categories/0/sections/0/emotes");
        family[1]["variant_of"] = "missing";
        Reject(variants.dump(), "/categories/0/sections/0/emotes");
        family[1]["variant_of"] = family[0]["id"];
        family[0]["variant_of"] = family[1]["id"];
        Reject(variants.dump(), "/categories/0/sections/0/emotes");

        auto base = Json::parse(text);
        base["categories"] = Json::array({base["categories"][0]});
        base["categories"][0]["sections"] = Json::array({base["categories"][0]["sections"][0]});
        auto& fixtureEmotes = base["categories"][0]["sections"][0]["emotes"];
        fixtureEmotes = Json::array({fixtureEmotes[0]});
        fixtureEmotes[0].erase("duration_seconds");
        const std::string ep = "/categories/0/sections/0/emotes/0";
        const auto field = [&](const std::string& path, const Json& value, bool valid) {
            auto document = base;
            document[Json::json_pointer(path)] = value;
            if (valid) {
                Accept(document.dump());
            } else {
                Reject(document.dump(), path);
            }
        };
        // Stop entries omit animation fields and reject unrelated action types.
        auto command = base;
        command[Json::json_pointer(ep)] = {{"id", "stop-pose"}, {"name", "Stop pose"}, {"action", "stop"}};
        Check(Accept(command.dump()).categories[0].sections[0].emotes[0].action == SkyEmote::EmoteAction::Stop,
            "Stop command was not preserved");
        command[Json::json_pointer(ep)]["event"] = "IdleWave";
        Reject(command.dump(), ep, "Unknown field");
        command[Json::json_pointer(ep)].erase("event");
        command[Json::json_pointer(ep)]["action"] = "script";
        Reject(command.dump(), ep + "/action");
        // Accept version 1, including its numeric equivalent, but reject other versions and Booleans.
        field("/schema", 2, false);
        field("/schema", true, false);
        field("/schema", 1.0, true);
        // Reject unsafe IDs while accepting long identifiers.
        field("/id", "../oops", false);
        field("/id", "vanilla\n", false);
        field("/id", std::string(64, 'a'), true);
        field("/id", std::string(1024, 'a'), true);
        // Require visible text and allow long Unicode labels.
        field("/name", "", false);
        field("/name", "  ", false);
        field("/name", "\n", false);
        field("/name", "Привет", true);
        field("/name", "\xC2\xA0", false);
        std::string emoji;
        for (int i = 0; i < 96; ++i) {
            emoji += "😀";
        }
        field("/name", emoji, true);
        field("/name", emoji + "😀", true);
        // Require nonempty collections, known modes and valid event-name characters/types.
        field("/categories", Json::array(), false);
        field(ep + "/mode", "repeat", false);
        field(ep + "/event", std::string(255, 'A'), true);
        field(ep + "/event", std::string(1024, 'A'), true);
        field(ep + "/event", "IdleWave\n", false);
        field(ep + "/event", "../foo.hkx", false);
        field(ep + "/event", 4, false);

        // Preserve the loop mode and reject missing or unknown fields.
        auto document = base;
        document[Json::json_pointer(ep)]["mode"] = "loop";
        Check(Accept(document.dump()).categories[0].sections[0].emotes[0].playback.mode ==
            SkyEmote::PlaybackMode::Loop, "Loop mode was not preserved");
        // Timers are optional, positive finite numbers and only apply to the matching mode.
        const auto defaults = Accept(base.dump()).categories[0].sections[0].emotes[0].playback;
        Check(defaults.duration_seconds == 0.0 && defaults.repeat_interval_seconds == 0.0,
            "Missing timing must rely on graph completion rather than a guessed timer");
        for (const auto& invalid : {Json(0), Json(-0.1), Json(true), Json(nullptr), Json("1.5")}) {
            field(ep + "/duration_seconds", invalid, false);
            auto loop = document;
            loop[Json::json_pointer(ep)]["repeat_interval_seconds"] = invalid;
            Reject(loop.dump(), ep + "/repeat_interval_seconds");
        }
        document[Json::json_pointer(ep)]["repeat_interval_seconds"] = 6.75;
        Check(Accept(document.dump()).categories[0].sections[0].emotes[0].playback.repeat_interval_seconds == 6.75,
            "Native repeat interval was not preserved");
        document[Json::json_pointer(ep)]["duration_seconds"] = 4.0;
        Reject(document.dump(), ep + "/duration_seconds", "only valid");
        document = base;
        document[Json::json_pointer(ep)]["repeat_interval_seconds"] = 6.75;
        Reject(document.dump(), ep + "/repeat_interval_seconds", "only valid");
        field(ep + "/duration_seconds", 4.1, true);
        field(ep + "/duration_seconds", 1e308, true);
        // Hidden choices remain addressable for saved IDs and active-pose exits.
        auto hiddenDocument = base;
        hiddenDocument[Json::json_pointer(ep)]["hidden"] = true;
        const auto hiddenPack = Accept(hiddenDocument.dump());
        Check(hiddenPack.categories[0].sections[0].emotes[0].hidden,
            "Hidden metadata was lost");
        const SkyEmote::PackCatalogue hiddenCatalogue{{hiddenPack}, {}};
        Check(static_cast<bool>(SkyEmote::FindEmote(hiddenCatalogue, {hiddenPack.id, "idlewave"})),
            "Hidden emote lost stable lookup");
        auto hiddenChild = hiddenDocument[Json::json_pointer(ep)];
        hiddenChild["id"] = "hidden-child";
        hiddenChild["hidden"] = false;
        hiddenChild["variant_of"] = "idlewave";
        hiddenDocument["categories"][0]["sections"][0]["emotes"].push_back(hiddenChild);
        Reject(hiddenDocument.dump(), "/categories/0/sections/0/emotes");
        hiddenDocument["categories"][0]["sections"][0]["emotes"][1]["hidden"] = true;
        Accept(hiddenDocument.dump());
        field(ep + "/hidden", "true", false);
        field(ep + "/allow_movement", true, true);
        field(ep + "/allow_movement", false, true);
        field(ep + "/allow_movement", "true", false);
        // Native IDLE references keep plugin-local IDs independent of load order.
        field(ep + "/idle_form", "Skyrim.esm|000F64D8", true);
        field(ep + "/idle_form", "Skyrim.esm|0f64d8", true);
        field(ep + "/idle_form", "Skyrim.esm|FFFFFF", true);
        for (const auto reference : {"", "|000F64D8", "Skyrim.esm", "Skyrim.esm|00000000",
                 "Skyrim.esm|010F64D8", "Skyrim.esm|000F64DZ", "Skyrim.esm|0x0F64D8",
                 "Skyrim.esm|F64D8", "Skyrim.esm|000F64D8|extra", "../Skyrim.esm|000F64D8",
                 "Data\\Skyrim.esm|000F64D8", "C:Skyrim.esm|000F64D8", "Skyrim.esm|00 F64D8",
                 "Skyrim.esm|000F64D8\n", "Skyrim\tesm|000F64D8"}) {
            field(ep + "/idle_form", reference, false);
        }
        field(ep + "/idle_form", 123, false);
        auto nativeDocument = base;
        nativeDocument[Json::json_pointer(ep)]["idle_form"] = "Skyrim.esm|000F64D8";
        const auto native = Accept(nativeDocument.dump()).categories[0].sections[0].emotes[0].playback.idle_form;
        Check(native && native->plugin == "Skyrim.esm" && native->id == 0xF64D8,
            "Native IDLE reference did not retain its plugin and numeric local ID");
        // Held states retain their pack scope and commit only after their authored entry time.
        auto stateDocument = base;
        auto& stateEmote = stateDocument[Json::json_pointer(ep)];
        stateEmote["mode"] = "loop";
        stateEmote["state"] = "Reading";
        stateEmote["entry_seconds"] = 2.0;
        stateEmote["state_only"] = true;
        auto held = Accept(stateDocument.dump()).categories[0].sections[0].emotes[0].playback;
        Check(held.state_scope == "vanilla" && held.state == "Reading" && held.entry_seconds == 2.0 && held.state_only,
            "Held-state definition lost scope, label, timing or idempotence");
        // Quickstart steps must reach the required state. Exits may omit setup and become no-ops.
        stateEmote["requires_state"] = "Reading";
        Check(Accept(stateDocument.dump()).categories[0].sections[0].emotes[0].playback.setup.empty(),
            "A state-dependent action may omit automatic setup");
        stateEmote["setup"] = Json::array({{{"event", "IdleReadBook"}, {"seconds", 2.0}, {"state_after", "Reading"}}});
        held = Accept(stateDocument.dump()).categories[0].sections[0].emotes[0].playback;
        Check(held.setup.size() == 1 && held.setup[0].event == "IdleReadBook" &&
            held.setup[0].seconds == 2.0 && held.setup[0].state_after == "Reading", "Setup steps were not retained");
        stateEmote["setup"][0]["seconds"] = 0;
        Reject(stateDocument.dump(), ep + "/setup/0/seconds", "positive");
        stateEmote["setup"][0]["seconds"] = Json::array({1});
        Reject(stateDocument.dump(), "", "nesting");
        stateEmote["setup"][0]["seconds"] = 2.0;
        stateEmote["setup"][0]["state_after"] = "Other";
        Reject(stateDocument.dump(), ep + "/setup", "reach");
        stateEmote["setup"][0]["state_after"] = "Reading";
        stateEmote["state_scope"] = "anotherpack";
        Reject(stateDocument.dump(), ep, "Unknown");
        auto exit = base;
        exit[Json::json_pointer(ep)]["requires_state"] = "Reading";
        Accept(exit.dump());
        field(ep + "/state", "Reading", false);
        field(ep + "/state_only", true, false);
        field(ep + "/entry_seconds", 1.0, false);
        // Reject missing required fields and unknown fields.
        document = base;
        document.erase("name");
        Reject(document.dump(), "/name", "Missing");
        document = base;
        document["surprise"] = true;
        Reject(document.dump(), "", "Unknown field");
        document = base;
        // Category IDs and emote IDs must be unique across the pack.
        document["categories"].push_back(document["categories"][0]);
        Reject(document.dump(), "/categories/1/id", "Duplicate ID");
        document["categories"][1]["id"] = "other";
        Reject(document.dump(), "/categories/1/sections/0/emotes/0/id", "Duplicate ID");
        // Distinct emotes may reuse section IDs across categories. Preserve category order.
        document["categories"][1]["sections"][0]["emotes"][0]["id"] = "otherwave";
        const auto ordered = Accept(document.dump());
        Check(ordered.categories[0].id == "social" && ordered.categories[1].id == "other",
            "Category order was not preserved");
        Check(ordered.categories[1].sections[0].id == "greetings", "Section IDs should be category-scoped");
        document = base;
        // Section IDs must remain unique within their own category.
        auto& sections = document["categories"][0]["sections"];
        sections.push_back(sections[0]);
        Reject(document.dump(), "/categories/0/sections/1/id", "Duplicate ID");

        // Reject duplicate keys, wrong roots, numeric overflow and nesting beyond the schema.
        Reject(R"({"schema":1,"schema":2})", "", "Duplicate JSON key");
        Reject("[]", "", "Expected an object");
        Reject(R"({"x":1e1000})", "", "range");
        Reject(std::string(9, '[') + "0" + std::string(9, ']'), "", "Expected an object");
        Reject(std::string(10, '[') + "0" + std::string(10, ']'), "", "nesting");
        auto nested = base;
        nested[Json::json_pointer(ep + "/event")] = Json::array({"IdleWave"});
        Reject(nested.dump(), ep + "/event", "Expected a string");
        // Manifest byte size is unrestricted.
        Accept(text + std::string(2 * 1024 * 1024, ' '));
        // Invalid UTF-8 must retain the filename and report a byte location.
        const auto invalidUtf8 = SkyEmote::ParsePack("encoding.json", "{\"x\":\"\xFF\"}");
        Check(!invalidUtf8 && invalidUtf8.error().file == "encoding.json" &&
            invalidUtf8.error().location.starts_with("byte "), "Invalid UTF-8 needs a byte diagnostic");

        // Large packs must retain every emote.
        auto large = base;
        large["categories"][0]["sections"] = Json::array();
        for (int s = 0; s < 4; ++s) {
            auto group = base["categories"][0]["sections"][0];
            group["id"] = "s" + std::to_string(s);
            group["emotes"] = Json::array();
            for (int e = 0; e < 1024; ++e) {
                auto item = base["categories"][0]["sections"][0]["emotes"][0];
                item["id"] = "e" + std::to_string(s * 1024 + e);
                group["emotes"].push_back(std::move(item));
            }
            large["categories"][0]["sections"].push_back(std::move(group));
        }
        Accept(large.dump());
        auto extra = base["categories"][0]["sections"][0];
        extra["id"] = "last";
        large["categories"][0]["sections"].push_back(std::move(extra));
        const auto expanded = Accept(large.dump());
        std::size_t emoteCount = 0;
        for (const auto& expandedCategory : expanded.categories) {
            for (const auto& expandedSection : expandedCategory.sections) {
                emoteCount += expandedSection.emotes.size();
            }
        }
        Check(emoteCount == 4097, "Pack entries beyond 4096 must be preserved");
        // Category, section and emote arrays have no item-count limits.
        for (const bool categories : {true, false}) {
            auto many = base;
            auto& items = categories ? many["categories"] : many["categories"][0]["sections"];
            const auto example = items[0];
            items = Json::array();
            for (int i = 0; i < 65; ++i) {
                auto item = example;
                item["id"] = "item" + std::to_string(i);
                auto& emote = categories ? item["sections"][0]["emotes"][0] : item["emotes"][0];
                emote["id"] = "emote" + std::to_string(i);
                items.push_back(std::move(item));
            }
            Accept(many.dump());
        }
        auto singleSection = base;
        auto& emotes = singleSection["categories"][0]["sections"][0]["emotes"];
        const auto example = emotes[0];
        emotes = Json::array();
        for (int i = 0; i < 1025; ++i) {
            auto item = example;
            item["id"] = "emote" + std::to_string(i);
            emotes.push_back(std::move(item));
        }
        Check(Accept(singleSection.dump()).categories[0].sections[0].emotes.size() == 1025,
            "A section must preserve entries beyond the old 1024 cap");
        std::cout << checks << " functional checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
