// SPDX-License-Identifier: MIT
#include "InputPolicy.h"
#include <algorithm>

namespace SkyEmote
{
    InputCommand ChooseInputCommand(unsigned code, const InputBindings& keys, const KeyContext& context)
    {
        if (context.mode == InputMode::Quick) return code == 1 ? InputCommand::EndQuick : InputCommand::QuickFavorite;
        if (context.mode == InputMode::Menu && context.binding != KeyCapture::None) return InputCommand::Bind;
        if (code == keys.menu && !context.typing && (context.mode == InputMode::Menu || context.can_open)) {
            return InputCommand::ToggleMenu;
        }
        if (context.mode == InputMode::None && !context.typing && context.gameplay_bindings) {
            if (keys.quick && code == keys.quick) return InputCommand::BeginQuick;
            if (keys.stop && code == keys.stop) return InputCommand::Stop;
        }
        if (context.mode == InputMode::Menu && code == 1) return InputCommand::CloseMenu;
        return InputCommand::None;
    }

    bool ValidInputBinding(unsigned code, KeyCapture target, const InputBindings& keys)
    {
        if (target != KeyCapture::Menu && target != KeyCapture::Stop && target != KeyCapture::Quick) return false;
        if (code > 255 || code == 1 || (target == KeyCapture::Menu && !code)) return false;
        return !code || ((target == KeyCapture::Menu || code != keys.menu) &&
            (target == KeyCapture::Stop || code != keys.stop) &&
            (target == KeyCapture::Quick || code != keys.quick));
    }

    std::optional<BindingChange> RebindInputKey(unsigned code, KeyCapture target, InputBindings keys)
    {
        if (!code || code > 255 || (target != KeyCapture::Menu && target != KeyCapture::Stop && target != KeyCapture::Quick)) return {};
        const bool restoring_menu = target == KeyCapture::Menu && code == 1;
        const auto replacement = code == 1 ? (restoring_menu ? 26u : 0u) : code;
        if (restoring_menu) {
            if (keys.stop == 26) keys.stop = 0;
            if (keys.quick == 26) keys.quick = 0;
        }
        if (!ValidInputBinding(replacement, target, keys)) return BindingChange{keys, true};
        (target == KeyCapture::Menu ? keys.menu : target == KeyCapture::Stop ? keys.stop : keys.quick) = replacement;
        return BindingChange{keys, false};
    }

    ContextPosition LocateInputContext(std::span<const std::uint32_t> captured,
        std::span<const std::uint32_t> current)
    {
        if (captured.empty() || current.size() < captured.size() ||
            !std::equal(captured.begin(), captured.end(), current.begin())) return ContextPosition::Replaced;
        return current.size() == captured.size() ? ContextPosition::Current : ContextPosition::Covered;
    }

    CaptureExit CheckCapture(InputMode mode, const CaptureConditions& conditions)
    {
        if (mode == InputMode::None) return CaptureExit::None;
        if (!conditions.focused) return CaptureExit::FocusLost;
        if (!conditions.gameplay) return CaptureExit::GameUnavailable;
        if (mode == InputMode::Menu) {
            return conditions.renderer ? CaptureExit::None : CaptureExit::RendererUnavailable;
        }
        if (conditions.context != ContextPosition::Current || conditions.foreign_typing) return CaptureExit::InputTaken;
        return conditions.timed_out ? CaptureExit::Timeout : CaptureExit::None;
    }
}
