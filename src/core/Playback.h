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
    // Input calls this on the main thread before Skyrim processes movement/jump/attack.
    void InterruptPlayback();
    void QueuePlaybackUpdate();
    void SetPlaybackReady(bool ready);
    std::shared_ptr<const CommandResult> GetCommandResult();
    std::shared_ptr<const PlaybackStatus> GetPlaybackStatus();
}
