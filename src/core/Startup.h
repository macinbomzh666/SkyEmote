// SPDX-License-Identifier: MIT
#pragma once

namespace SkyEmote
{
    // A normal load owns readiness forever after its first notification. Failed
    // loads must not fall back to an old player's still-loaded 3D.
    class InitialGameplay
    {
    public:
        void DataLoaded() { if (phase == Phase::BeforeData) phase = Phase::Waiting; }
        void UseLoadLifecycle() { phase = Phase::Managed; }
        bool Waiting() const { return phase == Phase::Waiting; }
        bool Enter()
        {
            if (!Waiting()) return false;
            phase = Phase::Managed;
            return true;
        }

    private:
        enum class Phase { BeforeData, Waiting, Managed };
        Phase phase = Phase::BeforeData;
    };

    // Called by the game-thread input listener, including frames with no key presses.
    void CheckInitialGameplay();
}
