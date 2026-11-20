// SPDX-License-Identifier: MIT
#include "FacePresets.h"
#include "PosePresetJson.h"

#include <fstream>
#include <mutex>
#include <utility>

namespace SkyEmote
{
    namespace
    {
        constexpr std::size_t max_presets = 128;
        static_assert(std::ranges::equal(FaceMoodNames, PosePresetJson::moods));
        static_assert(std::ranges::equal(FaceModifierNames, std::span(PosePresetJson::channels).subspan(2, 12)));
        static_assert(std::ranges::equal(FacePhonemeNames, std::span(PosePresetJson::channels).subspan(14)));

        std::once_flag load_built_in;
        std::vector<ExpressionPreset> built_in_presets;
        // The game thread reloads the shared files while the render thread can still hold a previous list.
        std::mutex presets_mutex;
        auto custom_presets = std::make_shared<const std::vector<ExpressionPreset>>();
        auto face_override = std::make_shared<const FaceOverrideSettings>();

        std::wstring PresetKey(std::string_view name)
        {
            constexpr std::wstring_view hex = L"0123456789abcdef";
            std::wstring key(CustomPresetPrefix);
            for (unsigned char ch : name) {
                if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                key += hex[ch >> 4];
                key += hex[ch & 15];
            }
            return key;
        }

        std::wstring BundledKey(const std::filesystem::path& file)
        {
            // Numbered filenames preserve menu order. The part after the dash is the stable INI key.
            const auto stem = file.stem().wstring();
            const auto separator = stem.find(L'-');
            if (separator == stem.npos || separator == 0 ||
                !std::ranges::all_of(stem.substr(0, separator), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) return {};
            return stem.substr(separator + 1);
        }

        std::vector<ExpressionPreset> ReadPresetFolder(const std::filesystem::path& folder, bool bundled)
        {
            std::vector<ExpressionPreset> presets;
            for (const auto& path : PosePresetJson::Files(folder)) {
                if (presets.size() == max_presets) break;
                std::ifstream input(path, std::ios::binary);
                const auto document = PosePresetJson::Read(input);
                ExpressionPreset preset{};
                PosePresetJson::Category category;
                PosePresetJson::Face pose;
                if (!PosePresetJson::Header(document, "face", preset.label, category) ||
                    !PosePresetJson::DecodeFace(PosePresetJson::Field(document, "pose"), pose)) continue;
                preset.key = bundled ? BundledKey(path) : PresetKey(preset.label);
                if (preset.key.empty() || std::ranges::find(presets, preset.key, &ExpressionPreset::key) != presets.end()) continue;
                preset.premade = category == PosePresetJson::Category::Premade;
                if (document.contains("source")) preset.source = document["source"].get<std::string>();
                if (pose.mood) preset.pose.mood = FaceMood{pose.mood->index, pose.mood->strength};
                std::copy_n(pose.weights.begin(), preset.pose.eyelids.size(), preset.pose.eyelids.begin());
                std::copy_n(pose.weights.begin() + 2, preset.pose.modifiers.size(), preset.pose.modifiers.begin());
                std::copy_n(pose.weights.begin() + 14, preset.pose.phonemes.size(), preset.pose.phonemes.begin());
                presets.push_back(std::move(preset));
            }
            return presets;
        }
    }

    void LoadBuiltInExpressionPresets(const std::filesystem::path& plugins)
    {
        std::call_once(load_built_in, [&] {
            built_in_presets = ReadPresetFolder(plugins / L"SkyEmote/Presets/Face", true);
        });
    }

    void LoadCustomExpressionPresets(const std::filesystem::path& plugins)
    {
        auto presets = ReadPresetFolder(plugins / L"PosePresets/Face", false);
        std::ranges::sort(presets, {}, &ExpressionPreset::key);
        auto updated = std::make_shared<const std::vector<ExpressionPreset>>(std::move(presets));
        const std::scoped_lock lock(presets_mutex);
        custom_presets = std::move(updated);
    }

    std::span<const ExpressionPreset> BuiltInExpressionPresets() { return built_in_presets; }

    std::shared_ptr<const std::vector<ExpressionPreset>> CustomExpressionPresets()
    {
        const std::scoped_lock lock(presets_mutex);
        return custom_presets;
    }

    const ExpressionPreset* FindExpressionPreset(std::span<const ExpressionPreset> custom, std::wstring_view key)
    {
        const auto choices = key.starts_with(CustomPresetPrefix) ? custom : BuiltInExpressionPresets();
        const auto found = std::ranges::find_if(choices, [key](const ExpressionPreset& preset) { return preset.key == key; });
        return found == choices.end() ? nullptr : &*found;
    }

    std::shared_ptr<const FaceOverrideSettings> GetFaceOverrideSettings()
    {
        const std::scoped_lock lock(presets_mutex);
        return face_override;
    }

    void SetFaceOverrideSettings(FaceOverrideSettings settings)
    {
        auto updated = std::make_shared<const FaceOverrideSettings>(std::move(settings));
        const std::scoped_lock lock(presets_mutex);
        face_override = std::move(updated);
    }
}
