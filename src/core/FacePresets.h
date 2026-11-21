// SPDX-License-Identifier: MIT
#pragma once

#include "Pack.h"

#include <memory>
#include <span>

namespace SkyEmote
{
    struct ExpressionPreset
    {
        std::wstring key;
        std::string label;
        FacePose pose;
        bool premade = false;
        std::string source;
    };

    inline constexpr std::wstring_view CustomPresetPrefix = L"face:";
    inline constexpr std::size_t PresetNameLimit = 63;
    // The INI key stores two hex digits for each byte of the UTF-8 preset name.
    inline constexpr auto ExpressionPresetKeyCapacity = CustomPresetPrefix.size() + 2 * PresetNameLimit + 1;

    void LoadBuiltInExpressionPresets(const std::filesystem::path& plugins);
    void LoadCustomExpressionPresets(const std::filesystem::path& plugins);
    std::shared_ptr<const std::vector<ExpressionPreset>> CustomExpressionPresets();
    std::span<const ExpressionPreset> BuiltInExpressionPresets();
    const ExpressionPreset* FindExpressionPreset(std::span<const ExpressionPreset> custom, std::wstring_view key);
}
