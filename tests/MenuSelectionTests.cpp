// SPDX-License-Identifier: MIT
#include "MenuSelection.h"
#include "Playback.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    try {
        using namespace SkyEmote;
        Pack pack{.id = "demo", .name = "Social"};
        Category category{.id = "greeting", .name = "Greetings"};
        Section section{.id = "wave", .name = "Waving", .emotes = {
            {.id = "wave", .name = "Wave"},
            {.id = "formal", .name = "Formal greeting", .variant_of = "wave"},
            {.id = "old", .name = "Old greeting", .variant_of = "wave", .hidden = true}}};
        // Remembered variants yield to a reachable search match, without losing family order.
        auto row = ResolveMenuRow(pack, category, section, section.emotes[0], "formal", "wave", {});
        Check(row.matches && row.chosen->id == "wave" && row.variants.size() == 2,
            "Search did not choose a visible matching variant");
        row = ResolveMenuRow(pack, category, section, section.emotes[0], "wave", "formal", {});
        Check(row.matches && row.chosen->id == "formal", "Variant-only search was unreachable");
        row = ResolveMenuRow(pack, category, section, section.emotes[0], "formal", FoldMenuSearch("SOCIAL"), {});
        Check(row.matches && row.chosen->id == "formal", "Pack-name search lost the remembered choice");
        row = ResolveMenuRow(pack, category, section, section.emotes[0], "old", "old greeting", {});
        Check(!row.matches && row.chosen->id == "wave", "Hidden variant became a browser choice");

        // An exit follows the actual held pose, including a hidden retained variant.
        Section exits{.id = "exits", .name = "Finish", .emotes = {
            {.id = "finish", .name = "Finish", .playback = {.requires_state = "standing"}},
            {.id = "finish-old", .name = "Finish old", .playback = {.requires_state = "seated"},
                .variant_of = "finish", .hidden = true}}};
        PlaybackStatus status{.state = "seated", .state_scope = "demo"};
        row = ResolveMenuRow(pack, category, exits, exits.emotes[0], {}, {}, status);
        Check(row.chosen->id == "finish-old" && row.variants.size() == 1, "Hidden matching exit was not selected");
        status.state_scope = "different-pack";
        row = ResolveMenuRow(pack, category, exits, exits.emotes[0], {}, {}, status);
        Check(row.chosen->id == "finish", "Another pack selected this pack's exit");
        status.state_scope = "demo";
        row = ResolveMenuRow(pack, category, exits, exits.emotes[0], {}, {}, status, true);
        Check(row.chosen->id == "finish", "Favorite was replaced by an active-state exit");

        // Vanilla rows and explicit favorites never inherit another row's search match.
        pack.id = "vanilla";
        row = ResolveMenuRow(pack, category, section, section.emotes[0], "formal", "formal", {});
        Check(!row.matches && row.variants.size() == 1, "Vanilla primary inherited its separate variant's search match");
        row = ResolveMenuRow(pack, category, section, section.emotes[1], {}, "formal", {}, true);
        Check(row.matches && row.chosen->id == "formal" && row.variants.size() == 1, "Favorite variant changed identity");
        pack.id = "demo";
        pack.aliases.emplace("saved-wave", "formal");
        category.sections.push_back(section);
        pack.categories.push_back(category);
        PackCatalogue catalogue{{pack}, {}};
        const auto found = FindEmote(catalogue, {"demo", "saved-wave"});
        Check(bool(found), "Saved alias did not resolve");
        row = ResolveMenuRow(*found.pack, *found.category, *found.section, *found.emote, "wave", {}, {}, true);
        Check(row.chosen->id == "formal", "Saved alias lost its explicit favorite variant");
        std::cout << "Menu selection checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
