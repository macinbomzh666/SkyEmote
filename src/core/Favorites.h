// SPDX-License-Identifier: MIT
#pragma once
#include "Pack.h"
#include <map>

namespace SkyEmote
{
    class Favorites
    {
    public:
        void Load(const std::filesystem::path& file, const std::filesystem::path& legacy = {});
        bool Toggle(const EmoteKey& key);
        bool BindHotkey(const EmoteKey& key, unsigned scan_code);
        unsigned Hotkey(const EmoteKey& key) const;
        std::optional<EmoteKey> FindHotkey(unsigned scan_code) const;
        // Copy resolved bindings for input-thread use. Retain missing entries so
        // pressing their key can end Quick Emote and report the unavailable pack.
        std::map<unsigned, std::optional<Emote>> ResolveHotkeys(const PackCatalogue& catalogue) const;
        const std::vector<EmoteKey>& Items() const { return items; }
        const std::string& Error() const { return error; }

    private:
        bool Save(const std::vector<EmoteKey>& candidate, const std::map<EmoteKey, unsigned>& bindings);
        std::filesystem::path file;
        std::map<EmoteKey, unsigned> hotkeys;
        std::vector<EmoteKey> items;
        std::string error;
        // A damaged or unreadable file must survive until the user repairs or removes it.
        bool writable = false;
    };
}
