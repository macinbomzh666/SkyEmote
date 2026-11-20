// SPDX-License-Identifier: MIT
#include "FacePlayback.h"

#include <RE/B/BSFaceGenAnimationData.h>
#include <RE/P/PlayerCharacter.h>
#include <SKSE/Logger.h>

namespace SkyEmote
{
    namespace
    {
        using Keyframe = RE::BSFaceGenKeyframeMultiple;
        constexpr auto firstModifier = Keyframe::BrowDownLeft;
        constexpr auto firstPhoneme = Keyframe::Aah;
        static_assert(Keyframe::SquintRight - firstModifier + 1 == FaceModifierNames.size());
        static_assert(Keyframe::W - firstPhoneme + 1 == FacePhonemeNames.size());
        static_assert(Keyframe::MoodDisgusted - Keyframe::MoodNeutral + 1 == FaceMoodNames.size());

        bool Has(const Keyframe& frame, unsigned index)
        {
            return frame.values && index < frame.count;
        }

        void SetWeight(Keyframe& frame, unsigned index, float value)
        {
            if (Has(frame, index) && frame.values[index] != value) {
                frame.SetValue(index, value);
            }
        }

        template <std::size_t N>
        void ApplyWeights(const std::array<std::optional<float>, N>& weights,
            Keyframe& input, Keyframe& output, unsigned first, bool clear)
        {
            for (unsigned i = 0; i < weights.size(); ++i) {
                if (!weights[i]) continue;
                const auto value = clear ? 0.0F : *weights[i];
                SetWeight(input, first + i, value);
                SetWeight(output, first + i, value);
            }
        }
    }

    RE::BSFaceGenAnimationData* FacePlayback::GetLiveFace(RE::PlayerCharacter* player) const
    {
        if (!face_ || !player || player->Get3D(false) != root_) return nullptr;
        auto* data = player->GetFaceGenAnimationData();
        return data == face_ ? data : nullptr;
    }

    void FacePlayback::Start(RE::PlayerCharacter& player, const FacePose& pose)
    {
        auto* data = player.GetFaceGenAnimationData();
        const auto* root = player.Get3D(false);
        if (!data || !root) {
            SKSE::log::debug("FaceGen skipped: player face or model unavailable");
            return;
        }

        face_ = data;
        root_ = root;
        pose_ = pose;
        RE::BSSpinLockGuard lock(data->lock);
        if (pose_.mood && !Has(data->expressionKeyFrame, Keyframe::MoodNeutral + pose_.mood->index)) {
            SKSE::log::debug("FaceGen mood unavailable: expressionSlots={}", data->expressionKeyFrame.count);
            pose_.mood.reset();
        }
        if (pose_.mood) data->ClearExpressionOverride();
        Apply(*data, false);
        SKSE::log::debug("FaceGen start: mood={} strength={} expressionSlots={}",
            pose_.mood ? FaceMoodNames[pose_.mood->index] : "none",
            pose_.mood ? pose_.mood->strength : 0.0F, data->expressionKeyFrame.count);
    }

    void FacePlayback::Apply(RE::BSFaceGenAnimationData& data, bool clear) const
    {
        if (pose_.mood && Has(data.expressionKeyFrame, Keyframe::MoodNeutral + pose_.mood->index)) {
            const auto index = Keyframe::MoodNeutral + pose_.mood->index;
            const auto value = clear ? 0.0F : pose_.mood->strength;
            if (clear) {
                data.ClearExpressionOverride();
            } else if (!data.exprOverride) {
                data.SetExpressionOverride(index, value);
                data.exprOverride = true;
            }
            // The engine blends its expression keyframes during playback. Keep
            // the authored mood in both the input and the rendered result.
            SetWeight(data.expressionKeyFrame, index, value);
            SetWeight(data.expression3, index, value);
        }
        ApplyWeights(pose_.modifiers, data.modifierKeyFrame, data.modifier3, firstModifier, clear);
        ApplyWeights(pose_.phonemes, data.phenomeKeyFrame, data.phoneme3, firstPhoneme, clear);
    }

    void FacePlayback::Update(RE::PlayerCharacter& player)
    {
        if (auto* data = GetLiveFace(&player)) {
            RE::BSSpinLockGuard lock(data->lock);
            Apply(*data, false);
        } else {
            *this = {};
        }
    }

    void FacePlayback::Release(RE::PlayerCharacter* player)
    {
        if (auto* data = GetLiveFace(player)) {
            RE::BSSpinLockGuard lock(data->lock);
            Apply(*data, true);
            SKSE::log::debug("FaceGen released: authored channels cleared");
        }
        *this = {};
    }
}
