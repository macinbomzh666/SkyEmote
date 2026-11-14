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
        // Search replaces the selected page without losing the last browsing destination.
        MenuNavigation navigation;
        Check(navigation.page == "@favorites", "Initial view did not select Favorites");
        navigation.SelectPage("demo");
        navigation.EditSearch(true);
        Check(navigation.page.empty() && navigation.last_browse == "demo", "Search kept a browsing tab selected");
        navigation.EditSearch(true);
        Check(navigation.page.empty() && navigation.last_browse == "demo", "Editing search lost the return page");
        navigation.EditSearch(false);
        Check(navigation.page == "demo", "Clearing search did not restore the pack page");
        navigation.EditSearch(true);
        navigation.SelectPage("@settings");
        Check(navigation.page == "@settings", "Selecting a tab did not leave search");
        navigation.EditSearch(true);
        navigation.EditSearch(false);
        Check(navigation.page == "@settings", "Search restored an older browsing page");
        navigation.SelectPage("@favorites");
        navigation.EditSearch(true);
        navigation.EditSearch(false);
        Check(navigation.page == "@favorites", "Clearing search did not restore Favorites");

        Pack pack{.id = "demo", .name = "Social"};
        Category category{.id = "greeting", .name = "Greetings"};
        Section section{.id = "wave", .name = "Waving", .emotes = {
            {.id = "wave", .name = "Wave"},
            {.id = "formal", .name = "Formal greeting", .variant_of = "wave"},
            {.id = "old", .name = "Old greeting", .variant_of = "wave", .hidden = true}}};
        // Remembered variants yield to a reachable search match, without losing family order.
        auto row = ResolveMenuRow(pack, section, section.emotes[0], "formal", "wave", {});
        Check(row.matches && row.chosen->id == "wave" && row.variants.size() == 2,
            "Search did not choose a visible matching variant");
        row = ResolveMenuRow(pack, section, section.emotes[0], "wave", "formal", {});
        Check(row.matches && row.chosen->id == "formal", "Variant-only search was unreachable");
        row = ResolveMenuRow(pack, section, section.emotes[0], "formal", {}, {});
        Check(row.matches && row.chosen->id == "formal", "Empty search lost the remembered choice");
        row = ResolveMenuRow(pack, section, section.emotes[0], "old", "old greeting", {});
        Check(!row.matches && row.chosen->id == "wave", "Hidden variant became a browser choice");

        // Only emote names match, even when a section or source name contains the query.
        for (const auto query : {"SOCIAL", "GREETINGS", "WAVING"}) {
            row = ResolveMenuRow(pack, section, section.emotes[1], {}, FoldMenuSearch(query), {});
            Check(!row.matches, "Source name matched an emote search");
        }
        row = ResolveMenuRow(pack, section, section.emotes[1], {}, FoldMenuSearch("FORMAL"), {});
        Check(row.matches && row.chosen->id == "formal", "Case-insensitive emote-name match was lost");
        row = ResolveMenuRow(pack, section, section.emotes[1], {}, "wave", {}, true);
        Check(!row.matches, "Favorite inherited a different variant's emote-name match");

        // An exit follows the actual held pose, including a hidden retained variant.
        Section exits{.id = "exits", .name = "Finish", .emotes = {
            {.id = "finish", .name = "Finish", .playback = {.requires_state = "standing"}},
            {.id = "finish-old", .name = "Finish old", .playback = {.requires_state = "seated"},
                .variant_of = "finish", .hidden = true}}};
        PlaybackStatus status{.state = "seated", .state_scope = "demo"};
        row = ResolveMenuRow(pack, exits, exits.emotes[0], {}, {}, status);
        Check(row.chosen->id == "finish-old" && row.variants.size() == 1, "Hidden matching exit was not selected");
        status.state_scope = "different-pack";
        row = ResolveMenuRow(pack, exits, exits.emotes[0], {}, {}, status);
        Check(row.chosen->id == "finish", "Another pack selected this pack's exit");
        status.state_scope = "demo";
        row = ResolveMenuRow(pack, exits, exits.emotes[0], {}, {}, status, true);
        Check(row.chosen->id == "finish", "Favorite was replaced by an active-state exit");

        // Vanilla rows and explicit favorites never inherit another row's search match.
        pack.id = "vanilla";
        row = ResolveMenuRow(pack, section, section.emotes[0], "formal", "formal", {});
        Check(!row.matches && row.variants.size() == 1, "Vanilla primary inherited its separate variant's search match");
        row = ResolveMenuRow(pack, section, section.emotes[1], {}, "formal", {}, true);
        Check(row.matches && row.chosen->id == "formal" && row.variants.size() == 1, "Favorite variant changed identity");
        pack.id = "demo";
        pack.aliases.emplace("saved-wave", "formal");
        category.sections.push_back(section);
        pack.categories.push_back(category);
        PackCatalogue catalogue{{pack}, {}};
        const auto found = FindEmote(catalogue, {"demo", "saved-wave"});
        Check(bool(found), "Saved alias did not resolve");
        row = ResolveMenuRow(*found.pack, *found.section, *found.emote, "wave", {}, {}, true);
        Check(row.chosen->id == "formal", "Saved alias lost its explicit favorite variant");
        // Results from every pack share one list with stable source identities and vanilla first.
        Pack native{.id = "vanilla", .name = "Base game", .categories = {
            {.id = "social", .name = "Conversation", .sections = {
                {.id = "greet", .name = "Salutations", .emotes = {
                    {.id = "wave", .name = "Wave"},
                    {.id = "hidden", .name = "Secret wave", .hidden = true}}}}}}};
        Pack other{.id = "other", .name = "Other pack", .categories = {
            {.id = "social", .name = "Conversation", .sections = {
                {.id = "greet", .name = "Salutations", .emotes = {
                    {.id = "wave", .name = "Small wave"}}}}}}};
        other.categories[0].sections.push_back({.id = "farewells", .name = "Parting", .emotes = {
            {.id = "wave-goodbye", .name = "Goodbye wave"}}});
        other.categories.push_back({.id = "gestures", .name = "Gestures", .sections = {
            {.id = "distant", .name = "Distant", .emotes = {
                {.id = "wave-wide", .name = "Wide wave"}}}}});
        PackCatalogue combined{{pack, other, native}, {}};
        VariantChoices choices{{{"demo", "wave"}, "formal"}};
        auto groups = SearchMenu(combined, "wave", {}, choices);
        Check(groups.size() == 5 && groups[0].pack->id == "vanilla" && groups[1].pack->id == "demo" &&
            groups[2].pack->id == "other", "Global search omitted a pack or lost browser order");
        for (const auto& group : groups) {
            Check(group.rows.size() == 1, "Search exposed hidden entries or duplicated addon variants");
        }
        Check(groups[3].rows[0].chosen->id == "wave-goodbye" && groups[4].rows[0].chosen->id == "wave-wide",
            "Search did not aggregate other sections and categories");
        Check(groups[1].rows[0].chosen->id == "wave", "Search did not reach a matching variant");
        groups = SearchMenu(combined, "formal", {}, choices);
        Check(groups.size() == 1 && groups[0].pack->id == "demo" && groups[0].rows[0].chosen->id == "formal",
            "Global search lost a variant-only match");

        // Pack/category/section labels provide context but never make unrelated emotes match.
        for (const auto query : {"social", "conversation", "salutations", "parting", "gestures", "distant"}) {
            Check(SearchMenu(combined, query, {}, choices).empty(), "Source labels expanded search results");
        }
        Check(SearchMenu(combined, {}, {}, choices).empty(), "Empty search listed the whole catalogue");
        Check(SearchMenu(combined, "no-such-emote", {}, choices).empty(), "Unmatched search returned rows");

        // Normal browsing ignores the global query and keeps remembered variants.
        groups = CollectMenuGroups(combined.packs[0], combined.packs[0].categories[0], {}, {}, choices);
        Check(groups.size() == 1 && groups[0].rows[0].chosen->id == "formal",
            "Unfiltered browsing lost its remembered variant");
        std::cout << "Menu selection checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
