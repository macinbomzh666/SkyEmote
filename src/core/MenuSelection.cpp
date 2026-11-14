// SPDX-License-Identifier: MIT
#include "MenuSelection.h"
#include "Playback.h"

#include <utility>

namespace SkyEmote
{
    void MenuNavigation::EditSearch(bool has_query)
    {
        page = has_query ? "" : last_browse;
    }

    void MenuNavigation::SelectPage(std::string_view id)
    {
        page = id;
        last_browse = id;
    }

    std::string FoldMenuSearch(std::string_view text)
    {
        std::string output(text);
        for (auto& c : output) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return output;
    }

    MenuRow ResolveMenuRow(const Pack& pack, const Section& section,
        const Emote& primary, std::string_view remembered, std::string_view query,
        const PlaybackStatus& status, bool favorite)
    {
        MenuRow row{&primary, &primary, {&primary}, false};
        if (!favorite && pack.id != "vanilla") {
            for (const auto& item : section.emotes) {
                if (!item.hidden && item.variant_of == primary.id) row.variants.push_back(&item);
            }
        }
        const auto matches = [&](const Emote& item) {
            return FoldMenuSearch(item.name).find(query) != std::string::npos;
        };
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

    std::vector<MenuGroup> CollectMenuGroups(const Pack& pack, const Category& category,
        std::string_view query, const PlaybackStatus& status, const VariantChoices& choices)
    {
        std::vector<MenuGroup> groups;
        for (const auto& section : category.sections) {
            MenuGroup group{&pack, &category, &section, {}};
            for (const auto& emote : section.emotes) {
                if (emote.hidden || (pack.id != "vanilla" && !emote.variant_of.empty())) continue;
                const auto remembered = choices.find({pack.id, emote.id});
                auto row = ResolveMenuRow(pack, section, emote,
                    remembered == choices.end() ? std::string_view{} : remembered->second, query, status);
                if (row.matches) group.rows.push_back(std::move(row));
            }
            if (!group.rows.empty()) groups.push_back(std::move(group));
        }
        return groups;
    }

    std::vector<MenuGroup> SearchMenu(const PackCatalogue& catalogue, std::string_view query,
        const PlaybackStatus& status, const VariantChoices& choices)
    {
        std::vector<MenuGroup> groups;
        if (query.empty()) return groups;
        const auto append = [&](const Pack& pack) {
            for (const auto& category : pack.categories) {
                for (auto& group : CollectMenuGroups(pack, category, query, status, choices)) {
                    groups.push_back(std::move(group));
                }
            }
        };
        // Match browser order even when the vanilla manifest sorts after an addon.
        for (const auto& pack : catalogue.packs) if (pack.id == "vanilla") append(pack);
        for (const auto& pack : catalogue.packs) if (pack.id != "vanilla") append(pack);
        return groups;
    }
}
