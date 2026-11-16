// SPDX-License-Identifier: MIT
#include "InputPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Event
    {
        unsigned id;
        Event* next = nullptr;
    };

    void CheckDispatch()
    {
        using SkyEmote::DispatchFilteredInput;
        // Every subset exercises removal at the head, middle and tail, including
        // all/none. Downstream receives each survivor once in its original order.
        std::array<Event, 5> events{{{0}, {1}, {2}, {3}, {4}}};
        for (unsigned i = 0; i + 1 < events.size(); ++i) events[i].next = &events[i + 1];
        auto* const head = events.data();
        for (unsigned mask = 0; mask < (1u << events.size()); ++mask) {
            unsigned routed = 0;
            unsigned calls = 0;
            DispatchFilteredInput(&head, [&](Event& event) {
                Check(event.id == routed++, "Input was routed twice or out of order");
                return (mask & (1u << event.id)) != 0;
            }, [&](Event* const* remaining) {
                ++calls;
                auto* node = *remaining;
                for (auto& event : events) {
                    if (mask & (1u << event.id)) continue;
                    Check(node == &event, "A kept event was lost, copied or reordered");
                    node = node->next;
                }
                Check(!node, "A consumed event reached downstream");
            });
            Check(calls == 1 && routed == events.size(), "Batch was not dispatched exactly once");
            Check(head == events.data(), "The caller's head changed");
            for (unsigned i = 0; i < events.size(); ++i) {
                Check(events[i].next == (i + 1 < events.size() ? &events[i + 1] : nullptr),
                    "An engine event link was not restored");
            }
        }

        // A null argument and a non-null empty batch keep their distinct shapes.
        unsigned calls = 0;
        auto unexpected = [](Event&) -> bool { throw std::runtime_error("Routed an empty batch"); };
        DispatchFilteredInput<Event>(nullptr, unexpected, [&](Event* const* remaining) {
            ++calls;
            Check(!remaining, "Null input became a non-null batch");
        });
        Event* empty = nullptr;
        DispatchFilteredInput(&empty, unexpected, [&](Event* const* remaining) {
            ++calls;
            Check(remaining && !*remaining, "Empty input lost its dispatch");
        });
        Check(calls == 2, "Empty dispatch was skipped or duplicated");

        // Another filtering hook can nest inside ours. Each restores only its
        // own edits, so its return preserves the outer filter until dispatch ends.
        DispatchFilteredInput(&head, [](Event& event) { return event.id == 1; }, [&](Event* const* outer) {
            Check((*outer)->next == &events[2], "Outer filter leaked a captured event");
            DispatchFilteredInput(outer, [](Event& event) { return event.id == 2; }, [&](Event* const* inner) {
                Check(*inner == head && (*inner)->next == &events[3], "Nested filter did not compose");
            });
            Check((*outer)->next == &events[2] && events[2].next == &events[3], "Nested return damaged the outer list");
        });
        Check(head->next == &events[1], "Nested dispatch left the engine list filtered");
    }

    void CheckButtonOwnership()
    {
        using namespace SkyEmote;
        std::unordered_set<std::uint64_t> owned;
        // A Stop hold and release must not hide mouse motion, a movement release
        // or an unrelated press in the same batch. A later press works normally.
        Check(ConsumeInputButton(owned, 27, true, true), "Stop press was not captured");
        std::array<Event, 4> events{{{27}, {1000}, {17}, {30}}};
        for (unsigned i = 0; i + 1 < events.size(); ++i) events[i].next = &events[i + 1];
        auto* head = events.data();
        for (bool stop_held : {true, false}) {
            DispatchFilteredInput(&head, [&](Event& event) {
                if (event.id == 1000) return false;
                return ConsumeInputButton(owned, event.id, event.id == 27 ? stop_held : event.id != 17, false);
            }, [&](Event* const* remaining) {
                Check(*remaining == &events[1] && events[1].next == &events[2] &&
                    events[2].next == &events[3] && !events[3].next, "Owned Stop hid unrelated input");
            });
        }
        Check(owned.empty() && !ConsumeInputButton(owned, 27, true, false), "Released Stop remained owned");

        // Search Backspace stays ours through release after closing. Device IDs
        // keep a controller button with the same code independent of the keyboard.
        Check(ConsumeInputButton(owned, 14, true, true), "Search Backspace escaped capture");
        Check(!ConsumeInputButton(owned, (std::uint64_t{2} << 32) | 14, true, false), "Ownership crossed devices");
        Check(ConsumeInputButton(owned, 14, true, false) && ConsumeInputButton(owned, 14, false, false),
            "Backspace leaked after closing the menu");
        Check(!ConsumeInputButton(owned, 14, true, false), "Backspace did not work after release");

        // Closing the menu takes effect between events. The closing press and
        // release stay owned, while the next unrelated press passes downstream.
        events = {{{1}, {30}, {1}, {1000}}};
        for (unsigned i = 0; i + 1 < events.size(); ++i) events[i].next = &events[i + 1];
        auto mode = InputMode::Menu;
        unsigned routed = 0;
        DispatchFilteredInput(&head, [&](Event& event) {
            const bool capture = mode != InputMode::None;
            if (routed++ == 0) {
                Check(ChooseInputCommand(event.id, {}, {mode, KeyCapture::None, false, true, true}) == InputCommand::CloseMenu,
                    "Escape did not close capture in the batch");
                mode = InputMode::None;
            }
            if (event.id == 1000) return capture;
            return ConsumeInputButton(owned, event.id, routed != 3, capture);
        }, [&](Event* const* remaining) {
            Check(*remaining == &events[1] && events[1].next == &events[3] && !events[3].next,
                "Closing capture leaked Escape or hid later input");
        });
        Check(owned.empty(), "Closing release left a button owned");
    }
}

int main()
{
    using namespace SkyEmote;
    try {
        InputBindings keys{26, 27, 28};
        KeyContext context{InputMode::None, KeyCapture::None, false, true, true};
        // Global shortcuts yield to typing and foreign contexts. Stop remains
        // available without a renderer, including the adapter's allowed TFC context.
        Check(ChooseInputCommand(27, keys, context) == InputCommand::Stop, "Stop was not routed");
        context.can_open = false;
        Check(ChooseInputCommand(27, keys, context) == InputCommand::Stop, "Stop required a renderer");
        Check(ChooseInputCommand(26, keys, context) == InputCommand::None, "Menu ignored availability");
        context.typing = true;
        Check(ChooseInputCommand(28, keys, context) == InputCommand::None, "Quick activated while typing");
        context.typing = false;
        context.gameplay_bindings = false;
        Check(ChooseInputCommand(27, keys, context) == InputCommand::None, "Stop ignored foreign ownership");

        // Apply the activation between two presses in one batch. The activation
        // key itself can be used as a favorite on the next press.
        context.gameplay_bindings = true;
        Check(ChooseInputCommand(28, keys, context) == InputCommand::BeginQuick, "Quick required a renderer");
        context.mode = InputMode::Quick;
        Check(ChooseInputCommand(28, keys, context) == InputCommand::QuickFavorite, "Second Quick press rearmed capture");
        Check(ChooseInputCommand(27, keys, context) == InputCommand::QuickFavorite, "Stop shadowed a Quick favorite");
        Check(ChooseInputCommand(1, keys, context) == InputCommand::EndQuick, "Escape did not end Quick");
        context.mode = InputMode::None;
        Check(ChooseInputCommand(30, keys, context) == InputCommand::None, "Later gameplay press remained captured by policy");

        // Rebinding owns Escape and the menu key. After rebinding, Escape closes
        // the menu. Search permits typing the menu key and suppresses globals.
        context = {InputMode::Menu, KeyCapture::Menu, true, true, false};
        Check(ChooseInputCommand(1, keys, context) == InputCommand::Bind, "Rebinding Escape closed the menu");
        Check(ChooseInputCommand(26, keys, context) == InputCommand::Bind, "Menu key escaped rebinding");
        context.binding = KeyCapture::Favorite;
        Check(ChooseInputCommand(1, keys, context) == InputCommand::Bind, "Favorite Escape closed the menu");
        context.binding = KeyCapture::None;
        Check(ChooseInputCommand(26, keys, context) == InputCommand::None, "Menu key did not type into search");
        Check(ChooseInputCommand(27, keys, context) == InputCommand::None, "Stop fired inside search");
        Check(ChooseInputCommand(1, keys, context) == InputCommand::CloseMenu, "Escape failed after rebinding");
        context.typing = false;
        Check(ChooseInputCommand(26, keys, context) == InputCommand::ToggleMenu, "Menu key did not close the menu");

        // Startup rejects invalid keys and conflicting optionals. Live Escape
        // restores the mandatory default and clears only a conflicting optional.
        Check(!ValidInputBinding(0, KeyCapture::Menu, keys) &&
            !ValidInputBinding(1, KeyCapture::Menu, keys) &&
            !ValidInputBinding(256, KeyCapture::Menu, keys), "Invalid menu key accepted");
        Check(ValidInputBinding(0, KeyCapture::Stop, keys) &&
            ValidInputBinding(0, KeyCapture::Quick, keys), "Optional key could not be unbound");
        Check(!ValidInputBinding(26, KeyCapture::Stop, keys) &&
            !ValidInputBinding(27, KeyCapture::Quick, keys), "Startup conflict was accepted");
        auto change = RebindInputKey(27, KeyCapture::Menu, keys);
        Check(change && change->conflict && change->keys.menu == 26 && change->keys.stop == 27,
            "Live conflict changed a binding");
        change = RebindInputKey(1, KeyCapture::Menu, {29, 26, 28});
        Check(change && !change->conflict && change->keys.menu == 26 && change->keys.stop == 0 && change->keys.quick == 28,
            "Menu default did not clear the conflicting Stop key");
        change = RebindInputKey(1, KeyCapture::Menu, {29, 27, 26});
        Check(change && !change->conflict && change->keys.quick == 0 && change->keys.stop == 27,
            "Menu default did not clear the conflicting Quick key");
        change = RebindInputKey(1, KeyCapture::Quick, keys);
        Check(change && !change->conflict && change->keys.quick == 0 && change->keys.menu == 26,
            "Escape did not unbind Quick");
        Check(!RebindInputKey(256, KeyCapture::Stop, keys) && !RebindInputKey(0, KeyCapture::Menu, keys),
            "Invalid live scan code changed a binding");

        // Context cleanup waits while covered, including by another cursor of
        // the same kind. A changed prefix or missing reference must never be popped.
        const std::array<std::uint32_t, 2> captured{0, 5};
        Check(LocateInputContext(captured, captured) == ContextPosition::Current, "Own context was not recognized");
        Check(LocateInputContext(captured, std::array<std::uint32_t, 3>{0, 5, 7}) == ContextPosition::Covered,
            "Foreign context was not recognized");
        Check(LocateInputContext(captured, std::array<std::uint32_t, 3>{0, 5, 5}) == ContextPosition::Covered,
            "Same-kind overlay was mistaken for our cursor reference");
        Check(LocateInputContext(captured, std::array<std::uint32_t, 2>{0, 7}) == ContextPosition::Replaced &&
            LocateInputContext(captured, std::array<std::uint32_t, 1>{0}) == ContextPosition::Replaced &&
            LocateInputContext({}, captured) == ContextPosition::Replaced, "Replaced context remained releasable");

        // Quick has no visible window and no text-input reference. Observable
        // takeovers end it before routing a key, without a playback command.
        CaptureConditions state{true, true, true, ContextPosition::Current, false, false};
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::None, "Own Quick context ended capture");
        state.renderer = false;
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::None &&
            CheckCapture(InputMode::Menu, state) == CaptureExit::RendererUnavailable, "Renderer loss used the wrong capture rules");
        state.renderer = true;
        state.foreign_typing = true;
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::InputTaken &&
            CheckCapture(InputMode::Menu, state) == CaptureExit::None, "Text-input ownership was confused with menu typing");
        context = {InputMode::None, KeyCapture::None, false, false, false};
        Check(ChooseInputCommand(28, keys, context) == InputCommand::None, "Foreign typing triggered a favorite after cleanup");
        state.foreign_typing = false;
        state.context = ContextPosition::Covered;
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::InputTaken, "Covered Quick capture stayed active");
        state.context = ContextPosition::Replaced;
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::InputTaken, "Replaced Quick capture stayed active");
        state.context = ContextPosition::Current;
        state.timed_out = true;
        Check(CheckCapture(InputMode::Quick, state) == CaptureExit::Timeout &&
            CheckCapture(InputMode::Menu, state) == CaptureExit::None, "Quick timeout affected the wrong mode");
        state.gameplay = false;
        Check(CheckCapture(InputMode::Menu, state) == CaptureExit::GameUnavailable &&
            CheckCapture(InputMode::Quick, state) == CaptureExit::GameUnavailable, "Load/readiness loss retained capture");
        state.focused = false;
        Check(CheckCapture(InputMode::Menu, state) == CaptureExit::FocusLost &&
            CheckCapture(InputMode::Quick, state) == CaptureExit::FocusLost, "Focus loss retained capture");
        Check(CheckCapture(InputMode::None, state) == CaptureExit::None, "Inactive capture requested another cleanup");
        CheckDispatch();
        CheckButtonOwnership();
        std::cout << "Input policy and dispatch checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
