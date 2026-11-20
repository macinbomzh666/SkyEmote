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
        void Start(RE::PlayerCharacter& player, const FacePose& pose);
        void Update(RE::PlayerCharacter& player);
        void Release(RE::PlayerCharacter* player);
        bool Active() const { return face_ != nullptr; }

    private:
        RE::BSFaceGenAnimationData* GetLiveFace(RE::PlayerCharacter* player) const;
        void Apply(RE::BSFaceGenAnimationData& data, bool clear) const;

        const RE::NiAVObject* root_{};
        const RE::BSFaceGenAnimationData* face_{};
        FacePose pose_{};
    };
}
