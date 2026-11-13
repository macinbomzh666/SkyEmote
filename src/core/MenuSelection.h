// SPDX-License-Identifier: MIT
#pragma once
#include "Pack.h"

namespace SkyEmote
{
    struct PlaybackStatus;

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
    MenuRow ResolveMenuRow(const Pack& pack, const Category& category, const Section& section,
        const Emote& primary, std::string_view remembered, std::string_view query,
        const PlaybackStatus& status, bool favorite = false);
}
