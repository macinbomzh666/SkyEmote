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
        std::cout << "Input policy checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
