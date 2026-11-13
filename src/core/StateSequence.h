// SPDX-License-Identifier: MIT
#pragma once

#include "Pack.h"
#include <optional>

namespace SkyEmote
{
    struct EnteringState
    {
        PlaybackStep step;
        double elapsed;
        bool main_event;
    };

    struct HeldState
    {
        std::string scope;
        std::string state;
        std::optional<EnteringState> entering;
    };

    struct StateSequence
    {
        std::vector<PlaybackStep> steps;
        std::string initial_state;
        bool reuse = false;
        bool no_op = false;
        // First step is already playing. Keep its accepted duration and elapsed clock.
        bool preserve_first = false;
        double first_elapsed = 0.0;
    };

    StateSequence PlanStateSequence(const std::string& event, const PlaybackOptions& options, const HeldState& current);
}
