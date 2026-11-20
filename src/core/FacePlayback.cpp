// SPDX-License-Identifier: MIT
#include "FacePlayback.h"

#include <algorithm>

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

        float ScaleWeight(float weight, float intensity)
        {
            return std::min(weight * intensity, 1.0F);
        }

        template <std::size_t N>
        void ApplyWeights(const std::array<std::optional<float>, N>& weights,
            Keyframe& input, Keyframe& output, unsigned first, float intensity)
        {
            for (unsigned i = 0; i < weights.size(); ++i) {
                if (!weights[i]) continue;
                const auto value = ScaleWeight(*weights[i], intensity);
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

    void FacePlayback::Start(RE::PlayerCharacter& player, const FacePose& pose, float intensity)
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
        Apply(*data, intensity);
        SKSE::log::debug("FaceGen start: mood={} strength={} intensity={} expressionSlots={}",
            pose_.mood ? FaceMoodNames[pose_.mood->index] : "none",
            pose_.mood ? pose_.mood->strength : 0.0F, intensity, data->expressionKeyFrame.count);
    }

    void FacePlayback::Apply(RE::BSFaceGenAnimationData& data, float intensity)
    {
        const bool clear = intensity == 0.0F;
        if (clear && applied_intensity_ == 0.0F) return;
        if (pose_.mood && Has(data.expressionKeyFrame, Keyframe::MoodNeutral + pose_.mood->index)) {
            const auto index = Keyframe::MoodNeutral + pose_.mood->index;
            const auto value = ScaleWeight(pose_.mood->strength, intensity);
            if (clear) {
                data.ClearExpressionOverride();
            } else if (intensity != applied_intensity_ || !data.exprOverride) {
                data.ClearExpressionOverride();
                data.SetExpressionOverride(index, value);
                data.exprOverride = true;
            }
            // The engine blends its expression keyframes during playback. Keep
            // the authored mood in both the input and the rendered result.
            SetWeight(data.expressionKeyFrame, index, value);
            SetWeight(data.expression3, index, value);
        }
        ApplyWeights(pose_.modifiers, data.modifierKeyFrame, data.modifier3, firstModifier, intensity);
        ApplyWeights(pose_.phonemes, data.phenomeKeyFrame, data.phoneme3, firstPhoneme, intensity);
        applied_intensity_ = intensity;
    }

    void FacePlayback::Update(RE::PlayerCharacter& player, float intensity)
    {
        // Keep the session at zero so the slider can bring back the current face.
        if (intensity == 0.0F && applied_intensity_ == 0.0F) return;
        if (auto* data = GetLiveFace(&player)) {
            RE::BSSpinLockGuard lock(data->lock);
            Apply(*data, intensity);
        } else {
            *this = {};
        }
    }

    void FacePlayback::Release(RE::PlayerCharacter* player)
    {
        if (applied_intensity_ > 0.0F) {
            if (auto* data = GetLiveFace(player)) {
                RE::BSSpinLockGuard lock(data->lock);
                Apply(*data, 0.0F);
                SKSE::log::debug("FaceGen released: authored channels cleared");
            }
        }
        *this = {};
    }
}
