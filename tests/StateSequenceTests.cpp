// SPDX-License-Identifier: MIT
#include "StateSequence.h"
#include <iostream>
#include <stdexcept>

namespace
{
    int checks = 0;
    void Check(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
        ++checks;
    }
}

int main()
{
    using namespace SkyEmote;
    try {
        PlaybackOptions page;
        page.mode = PlaybackMode::Loop;
        page.state_scope = "vanilla";
        page.state = page.requires_state = "Reading";
        page.setup = {{"Read", 3.0, "Reading"}};

        // Quickstart enters from standing. An achieved state skips entry entirely.
        auto plan = PlanStateSequence("Page", page, {});
        Check(!plan.reuse && plan.steps.size() == 2 && plan.steps[0].event == "Read", "Missing quickstart entry");
        HeldState reading{"vanilla", "Reading", {}};
        plan = PlanStateSequence("Page", page, reading);
        Check(plan.reuse && plan.steps.size() == 1 && plan.steps[0].event == "Page", "Reading replayed its entry");

        // Matching labels/events in another pack must not share graph state.
        plan = PlanStateSequence("Page", page, {"other", "Reading", EnteringState{{"Read", 3.0, "Reading"}, 1.0, true}});
        Check(!plan.reuse && !plan.preserve_first && plan.steps.size() == 2, "State leaked between packs");

        // Preserve an accepted entry's elapsed time and duration when selecting its follow-up.
        HeldState entering{"vanilla", "", EnteringState{{"Read", 3.0, "Reading"}, 1.25, true}};
        page.setup[0].seconds = 4.0;
        plan = PlanStateSequence("Page", page, entering);
        Check(plan.reuse && plan.preserve_first && plan.first_elapsed == 1.25 &&
            plan.steps[0].seconds == 3.0 && plan.steps[1].event == "Page", "Entry clock was restarted or reinterpreted");

        // State-only buttons do nothing for an achieved state or their own running primary entry.
        PlaybackOptions read;
        read.mode = PlaybackMode::Loop;
        read.state_scope = "vanilla";
        read.state = "Reading";
        read.state_only = true;
        read.entry_seconds = 3.0;
        Check(PlanStateSequence("Read", read, reading).no_op, "Achieved state was restarted");
        Check(PlanStateSequence("Read", read, entering).no_op, "Running primary entry was restarted");
        Check(PlanStateSequence("Read", read,
            {"vanilla", "Reading", EnteringState{{"Page", 3.5, "Reading"}, 1.0, true}}).no_op,
            "An action within reading caused the parent reading entry to replay");

        // Selecting the entry alone cancels a pending follow-up without replaying that entry.
        entering.entering->main_event = false;
        plan = PlanStateSequence("Read", read, entering);
        Check(!plan.no_op && plan.preserve_first && plan.steps.size() == 1 && plan.first_elapsed == 1.25,
            "Pending page survived selection of the entry alone");

        // An exit is a no-op outside its own state, but completes an in-flight entry before exiting.
        PlaybackOptions end;
        end.state_scope = "vanilla";
        end.requires_state = "Reading";
        Check(PlanStateSequence("EndRead", end, {}).no_op, "Inactive exit started a new entry");
        Check(PlanStateSequence("EndRead", end, {"other", "Reading", {}}).no_op, "Exit affected another pack");
        plan = PlanStateSequence("EndRead", end, reading);
        Check(plan.reuse && plan.steps.size() == 1 && plan.steps[0].event == "EndRead", "Exit reset an achieved state");
        plan = PlanStateSequence("EndRead", end, entering);
        Check(plan.reuse && plan.preserve_first && plan.steps.size() == 2 && plan.steps[1].event == "EndRead",
            "Exit discarded an accepted entry");

        // Multi-stage quickstart resumes after an achieved intermediate state or within its next entry.
        PlaybackOptions float_action;
        float_action.mode = PlaybackMode::Loop;
        float_action.state_scope = "vanilla";
        float_action.state = float_action.requires_state = "Floating";
        float_action.setup = {{"Hover", 2.0, "Hovering"}, {"Rise", 3.0, "Floating"}};
        plan = PlanStateSequence("FloatAction", float_action, {"vanilla", "Hovering", {}});
        Check(plan.reuse && plan.steps.size() == 2 && plan.steps[0].event == "Rise", "Intermediate state was lost");
        plan = PlanStateSequence("FloatAction", float_action,
            {"vanilla", "Hovering", EnteringState{{"Rise", 3.0, "Floating"}, 2.0, true}});
        Check(plan.preserve_first && plan.first_elapsed == 2.0 && plan.steps.size() == 2 && plan.steps[0].event == "Rise",
            "In-flight rise was restarted");
        plan = PlanStateSequence("FloatAction", float_action, {"vanilla", "Floating", {}});
        Check(plan.reuse && plan.steps.size() == 1, "Achieved floating state ran quickstart again");
        // A previous state is no longer safely idle once a different transition has started.
        PlaybackOptions hover = read;
        hover.state = "Hovering";
        plan = PlanStateSequence("Hover", hover,
            {"vanilla", "Hovering", EnteringState{{"Rise", 3.0, "Floating"}, 1.0, true}});
        Check(!plan.no_op && !plan.reuse, "Selecting a prior state preserved an unwanted pending rise");
        // Lowering is a related transition without quickstart, not a reset to standing.
        PlaybackOptions lower;
        lower.mode = PlaybackMode::Loop;
        lower.state_scope = "vanilla";
        lower.requires_state = "Floating (raised)";
        lower.state = "Floating";
        lower.entry_seconds = 5.2;
        lower.state_only = true;
        Check(PlanStateSequence("Lower", lower, {}).no_op, "Lowering quickstarted from standing");
        plan = PlanStateSequence("Lower", lower, {"vanilla", "Floating (raised)", {}});
        Check(plan.reuse && plan.steps.size() == 1 && plan.steps[0].state_after == "Floating",
            "Lowering should retain the lower state without reset");
        plan = PlanStateSequence("Lower", lower,
            {"vanilla", "Floating", EnteringState{{"Rise", 5.2, "Floating (raised)"}, 2.0, true}});
        Check(plan.preserve_first && plan.first_elapsed == 2.0 && plan.steps.size() == 2,
            "Lowering lost an accepted rise");
        // The native exit's lowered pose is not the original entry state that accepts Rise.
        plan = PlanStateSequence("FloatAction", float_action, {"vanilla", "Floating (lowered)", {}});
        Check(!plan.reuse && plan.steps.size() == 3 && plan.steps[0].event == "Hover",
            "A lowered exit pose incorrectly skipped re-entry before rising");
        std::cout << checks << " state-sequence checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
