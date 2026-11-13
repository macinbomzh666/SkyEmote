// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>

namespace SkyEmote
{
    void LoadMenuFavorites(const std::filesystem::path& file, const std::filesystem::path& legacy = {});
    void DrawMenu();
    // Called on the render thread for raw keyboard presses captured by the open menu.
    bool HandleFavoriteHotkey(unsigned scan_code);
    // Uses an immutable snapshot of bindings, without accessing ImGui or mutable favorites.
    bool PlayQuickFavoriteHotkey(unsigned scan_code);
}
