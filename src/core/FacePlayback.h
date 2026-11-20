// SPDX-License-Identifier: MIT
#pragma once

#include "Pack.h"

namespace RE
{
    class BSFaceGenAnimationData;
    class NiAVObject;
    class PlayerCharacter;
}

namespace SkyEmote
{
    class FacePlayback
    {
    public:
        void Start(RE::PlayerCharacter& player, const FacePose& pose, float intensity);
        void Update(RE::PlayerCharacter& player, float intensity);
        void Release(RE::PlayerCharacter* player);
        bool Active() const { return face_ != nullptr; }

    private:
        struct BlinkState
        {
            std::uint32_t stage;
            float timer;
        };

        RE::BSFaceGenAnimationData* GetLiveFace(RE::PlayerCharacter* player) const;
        void Apply(RE::BSFaceGenAnimationData& data, float intensity);

        const RE::NiAVObject* root_{};
        const RE::BSFaceGenAnimationData* face_{};
        FacePose pose_{};
        float applied_intensity_ = 0.0F;
        std::optional<BlinkState> blink_;
    };
}
