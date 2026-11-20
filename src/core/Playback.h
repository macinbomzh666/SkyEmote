// SPDX-License-Identifier: MIT
#pragma once

#include "Pack.h"

#include <memory>
#include <string>

namespace SkyEmote
{
    struct CommandResult
    {
        std::string message;
        bool error = false;
    };

    struct PlaybackStatus
    {
        std::string state;
        bool transitioning = false;
        // Pack owning the held state. Empty when inactive, so another pack cannot select its exits.
        std::string state_scope;
    };

    void QueuePlay(std::string event, std::string name = {}, PlaybackOptions options = {});
    void QueueStop();
    inline constexpr unsigned DefaultFacePlaybackIntensity = 85;
    inline constexpr unsigned MaxFacePlaybackIntensity = 150;
    unsigned FacePlaybackIntensity();
    void SetFacePlaybackIntensity(unsigned percent);
    void QueueFacePreview(FacePose pose);
    bool FacePreviewActive();
    // Input calls this on the main thread before Skyrim processes movement/jump/attack.
    void InterruptPlayback();
    void QueuePlaybackUpdate();
    // Called from SKSE's game-thread pre-load message while the old face may still be live.
    void ReleasePlaybackFaceBeforeLoad();
    void SetPlaybackReady(bool ready);
    std::shared_ptr<const CommandResult> GetCommandResult();
    std::shared_ptr<const PlaybackStatus> GetPlaybackStatus();
}
