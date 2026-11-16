// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
#include <span>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace SkyEmote
{
    enum class InputMode { None, Menu, Quick };
    enum class KeyCapture { None, Menu, Stop, Quick, Favorite };
    enum class InputCommand { None, Bind, QuickFavorite, EndQuick, ToggleMenu, BeginQuick, Stop, CloseMenu };

    struct InputBindings
    {
        unsigned menu = 26;
        unsigned stop = 0;
        unsigned quick = 0;
    };

    struct KeyContext
    {
        InputMode mode;
        KeyCapture binding;
        bool typing;
        bool can_open;
        bool gameplay_bindings;
    };

    // The adapter checks capture validity before routing each press.
    InputCommand ChooseInputCommand(unsigned code, const InputBindings& keys, const KeyContext& context);
    bool ValidInputBinding(unsigned code, KeyCapture target, const InputBindings& keys);
    // Invalid scan codes return no change. Conflicts preserve the bindings and capture.
    struct BindingChange { InputBindings keys; bool conflict; };
    std::optional<BindingChange> RebindInputKey(unsigned code, KeyCapture target, InputBindings keys);

    // The adapter combines device and code into an identity. Ownership lasts
    // through release even if capture ends while the button is still pressed.
    bool ConsumeInputButton(std::unordered_set<std::uint64_t>& owned, std::uint64_t identity,
        bool pressed, bool capture);

    // Borrow the engine's nodes for one dispatch. Restore our link edits when
    // the downstream chain returns, without changing the caller's head.
    template <class Event, class Consume, class Dispatch>
    void DispatchFilteredInput(Event* const* events, Consume&& consume, Dispatch&& dispatch)
    {
        if (!events) {
            dispatch(events);
            return;
        }
        struct RestoreLinks
        {
            std::vector<std::pair<Event*, Event*>> changed;

            void SetNext(Event* event, Event* next)
            {
                if (event->next == next) return;
                changed.emplace_back(event, event->next);
                event->next = next;
            }

            ~RestoreLinks()
            {
                for (const auto& [event, next] : changed) event->next = next;
            }
        } links;
        Event* head = nullptr;
        Event* tail = nullptr;
        for (auto* event = *events; event;) {
            auto* next = event->next;
            if (!consume(*event)) {
                if (tail) links.SetNext(tail, event);
                else head = event;
                tail = event;
            }
            event = next;
        }
        if (tail) links.SetNext(tail, nullptr);
        dispatch(&head);
    }

    // A newer context can cover ours. Wait for it to leave before popping our
    // reference. A replaced stack gives us no reference that is safe to pop.
    enum class ContextPosition { Current, Covered, Replaced };
    ContextPosition LocateInputContext(std::span<const std::uint32_t> captured,
        std::span<const std::uint32_t> current);

    enum class CaptureExit { None, FocusLost, GameUnavailable, RendererUnavailable, InputTaken, Timeout };
    struct CaptureConditions
    {
        bool focused;
        bool gameplay;
        bool renderer;
        ContextPosition context;
        bool foreign_typing;
        bool timed_out;
    };
    CaptureExit CheckCapture(InputMode mode, const CaptureConditions& conditions);
}
