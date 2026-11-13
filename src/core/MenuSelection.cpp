// SPDX-License-Identifier: MIT
#include "MenuSelection.h"
#include "Playback.h"

namespace SkyEmote
{
    std::string FoldMenuSearch(std::string_view text)
    {
        std::string output(text);
        for (auto& c : output) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return output;
    }

    MenuRow ResolveMenuRow(const Pack& pack, const Category& category, const Section& section,
        const Emote& primary, std::string_view remembered, std::string_view query,
        const PlaybackStatus& status, bool favorite)
    {
        MenuRow row{&primary, &primary, {&primary}, false};
        if (!favorite && pack.id != "vanilla") {
            for (const auto& item : section.emotes) {
                if (!item.hidden && item.variant_of == primary.id) row.variants.push_back(&item);
            }
        }
        const auto contains = [&](std::string_view text) { return FoldMenuSearch(text).find(query) != std::string::npos; };
        const bool group_matches = query.empty() || contains(pack.name) || contains(category.name) || contains(section.name);
        const auto matches = [&](const Emote& item) { return group_matches || contains(item.name); };
        for (const auto* item : row.variants) {
            if (item->id == remembered) row.chosen = item;
            row.matches |= matches(*item);
        }
        // A variant-only search match must remain reachable from its primary row.
        if (!matches(*row.chosen)) {
            for (const auto* item : row.variants) {
                if (matches(*item)) { row.chosen = item; break; }
            }
        }
        // Hidden exits can end a retained pose, but another pack's state cannot
        // select them. Favorites always keep the exact saved emote.
        if (!favorite && status.state_scope == pack.id && !status.state.empty() &&
            row.chosen->playback.state.empty() && !row.chosen->playback.requires_state.empty()) {
            for (const auto& item : section.emotes) {
                if ((item.id == primary.id || item.variant_of == primary.id) &&
                    item.playback.requires_state == status.state) {
                    row.chosen = &item;
                    break;
                }
            }
        }
        return row;
    }
}
