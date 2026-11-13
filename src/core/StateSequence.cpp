// SPDX-License-Identifier: MIT
#include "StateSequence.h"

namespace SkyEmote
{
    StateSequence PlanStateSequence(const std::string& event, const PlaybackOptions& options, const HeldState& current)
    {
        StateSequence plan;
        const bool same_scope = current.scope == options.state_scope;
        const bool entering = same_scope && current.entering && !current.entering->step.state_after.empty();
        const bool exit = !options.requires_state.empty() && options.state.empty();

        // A state-only button is idempotent once its target is achieved or its own entry is running.
        // An entry being used as another action's setup still needs that pending action replaced.
        if (same_scope && options.state_only && !options.state.empty() &&
            ((current.state == options.state && (!current.entering || (entering &&
                current.entering->main_event && current.entering->step.state_after == options.state))) ||
                (entering && current.entering->main_event &&
                current.entering->step.state_after == options.state && current.entering->step.event == event))) {
            plan.no_op = true;
            return plan;
        }
        if ((exit || (!options.requires_state.empty() && options.setup.empty())) &&
            !(same_scope && current.state == options.requires_state) &&
            !(entering && current.entering->step.state_after == options.requires_state)) {
            // Without quickstart, an unrelated/inactive state's action must leave playback alone.
            plan.no_op = true;
            return plan;
        }

        std::size_t skip = 0;
        if (same_scope && !options.requires_state.empty()) {
            if (current.state == options.requires_state) {
                skip = options.setup.size();
                plan.reuse = true;
            } else if (!current.state.empty()) {
                for (std::size_t i = 0; i < options.setup.size(); ++i) {
                    if (options.setup[i].state_after == current.state) {
                        skip = i + 1;
                        plan.reuse = true;
                    }
                }
            }
        }

        if (entering) {
            const auto& running = *current.entering;
            if (options.state_only && event == running.step.event && options.state == running.step.state_after) {
                // Keep the accepted entry, but discard the older request's later action.
                plan.steps.push_back(running.step);
                plan.reuse = plan.preserve_first = true;
                plan.first_elapsed = running.elapsed;
                plan.initial_state = current.state;
                return plan;
            }
            for (std::size_t i = 0; i < options.setup.size(); ++i) {
                if (options.setup[i].event == running.step.event && options.setup[i].state_after == running.step.state_after) {
                    skip = i + 1;
                    plan.reuse = plan.preserve_first = true;
                    break;
                }
            }
            if ((exit || options.setup.empty()) && running.step.state_after == options.requires_state) {
                plan.reuse = plan.preserve_first = true;
            }
            if (plan.preserve_first) {
                // Duration belongs to the entry that actually started, even if new metadata differs.
                plan.steps.push_back(running.step);
                plan.first_elapsed = running.elapsed;
            }
        }

        if (plan.reuse) {
            plan.initial_state = current.state;
        }
        plan.steps.insert(plan.steps.end(), options.setup.begin() + skip, options.setup.end());
        plan.steps.push_back({event, options.entry_seconds, options.state});
        return plan;
    }
}
