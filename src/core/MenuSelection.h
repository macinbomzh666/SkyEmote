// SPDX-License-Identifier: MIT
#pragma once
#include "Pack.h"

#include <map>

namespace SkyEmote
{
    struct PlaybackStatus;

    struct MenuNavigation
    {
        // An empty page shows global results with no browsing tab selected.
        std::string page = "@favorites";
        // Retain the last browsing page while search replaces its contents.
        std::string last_browse = "@favorites";

        void EditSearch(bool has_query);
        void SelectPage(std::string_view id);
    };

    // Borrowed catalogue entries stay valid until the catalogue is replaced.
    struct MenuRow
    {
        const Emote* primary;
        const Emote* chosen;
        std::vector<const Emote*> variants;
        bool matches;
    };

    std::string FoldMenuSearch(std::string_view text);
    // query is already folded with FoldMenuSearch. Favorites keep the exact entry.
    MenuRow ResolveMenuRow(const Pack& pack, const Section& section,
        const Emote& primary, std::string_view remembered, std::string_view query,
        const PlaybackStatus& status, bool favorite = false);

    using VariantChoices = std::map<EmoteKey, std::string>;
    struct MenuGroup
    {
        const Pack* pack;
        const Category* category;
        const Section* section;
        std::vector<MenuRow> rows;
    };

    std::vector<MenuGroup> CollectMenuGroups(const Pack& pack, const Category& category,
        std::string_view query, const PlaybackStatus& status, const VariantChoices& choices);
    // Empty search restores browsing instead of listing the entire catalogue.
    std::vector<MenuGroup> SearchMenu(const PackCatalogue& catalogue, std::string_view query,
        const PlaybackStatus& status, const VariantChoices& choices);
}
