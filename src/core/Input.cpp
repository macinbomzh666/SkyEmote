// SPDX-License-Identifier: MIT
#include "Input.h"
#include "InputPolicy.h"

#include "Menu.h"
#include "MenuCamera.h"
#include "Playback.h"
#include "Startup.h"
#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/ButtonEvent.h>
#include <RE/C/CharEvent.h>
#include <RE/C/ControlMap.h>
#include <RE/C/CursorMenu.h>
#include <RE/I/INISettingCollection.h>
#include <RE/I/INIPrefSettingCollection.h>
#include <RE/M/MenuCursor.h>
#include <RE/H/HeldStateHandler.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerControls.h>
#include <RE/U/UI.h>
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <utility>

namespace SkyEmote
{
    namespace
    {
        struct Event
        {
            enum Kind { Key, Character, Mouse, Wheel, Motion, Shortcut, Position } kind;
            unsigned code{};
            bool down{};
            float x{}, y{};
        };

        // Settings edits originate on the render thread. Input reads them on the game thread,
        // so atomics make changes live without passing ImGui state into input callbacks.
        std::atomic<unsigned> menu_key{26};
        // Zero leaves the optional global Stop shortcut unbound.
        std::atomic<unsigned> stop_key{0};
        std::atomic<unsigned> quick_key{0};
        // Published for render-thread update scheduling. The start time is game-thread only.
        std::atomic<InputMode> capture_mode{InputMode::None};
        std::chrono::steady_clock::time_point quick_started{};
        double quick_timeout_seconds = 5.0;
        std::atomic<bool> binding_conflict{false};
        // Raw mouse counts need a user multiplier because hardware DPI is not part of the event.
        std::atomic<float> mouse_sensitivity{1.0F};
        std::atomic<float> sensitivity_min{1.0F}, sensitivity_max{1.0F};
        std::atomic<bool> sensitivity_limits_available{false};
        // Native mouse events use integer counts. Carry fractional counts between events
        // so a multiplier below 1.0 does not lose slow, one-count movements.
        double mouse_remainder_x{}, mouse_remainder_y{};
        // Input must know which editor owns the next press before handling Escape or the
        // menu key. Favorites themselves remain owned by the render thread.
        std::atomic<KeyCapture> key_capture{KeyCapture::None};
        // Edits set this flag. Saving clears it before writing, so a newer edit stays dirty.
        std::atomic<bool> settings_dirty{false};
        std::filesystem::path settings_file;
        bool installed{};
        // ImGui publishes whether an edit field owns typing. The menu hotkey then types normally.
        std::atomic<bool> text_input{false};
        // Bits enabled before opening that we turned off. Closing restores only these bits.
        std::uint32_t disabled_controls{};
        // Snapshot taken after pushing our cursor. Only the game thread reads or releases it.
        // Context IDs cannot distinguish an identical pop/push between observations.
        RE::ControlMap* captured_controls = nullptr;
        std::vector<std::uint32_t> captured_context;
        // Render updates keep cleanup running if another context still covers ours.
        std::atomic<bool> context_cleanup_pending{false};
        // Keep consuming buttons pressed in our menu until release, even after it closes.
        std::unordered_set<std::uint64_t> owned_buttons;

        // Copy input into this queue. Only the render thread touches ImGui's context.
        std::mutex input_mutex;
        std::vector<Event> pending;
        bool reset_input = true;
        // Protected by input_mutex so the footer can read game-thread save failures.
        std::string settings_error;

        // Renderer-end publishes availability and schedules at most one main-thread update.
        std::atomic<bool> renderer_available{false};
        std::atomic<bool> update_pending{false};

        // Load notifications advance input_session_id immediately. Main-thread cleanup
        // copies that ID into ready_session_id after load/new-game or initial console entry.
        // Different IDs prevent opening while loading or before cleanup finishes.
        std::atomic<std::uint64_t> input_session_id{1};
        std::atomic<std::uint64_t> ready_session_id{0};

        bool HasFocus()
        {
            DWORD process{};
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            return process == GetCurrentProcessId();
        }

        bool CanUseHotkeys()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            return ready_session_id.load() == input_session_id.load() &&
                HasFocus() && player && player->Is3DLoaded() &&
                !player->IsDead() && ui && !ui->GameIsPaused() && !ui->IsApplicationMenuOpen();
        }

        bool CanOpen()
        {
            return !context_cleanup_pending.load() && renderer_available.load() && CanUseHotkeys();
        }

        void CaptureKey(KeyCapture target, bool active)
        {
            binding_conflict.store(false);
            if (active && IsMenuOpen()) key_capture.store(target);
            else {
                auto expected = target;
                key_capture.compare_exchange_strong(expected, KeyCapture::None);
            }
        }

        void ClearKey(std::atomic<unsigned>& key)
        {
            key_capture.store(KeyCapture::None);
            binding_conflict.store(false);
            key.store(0);
            settings_dirty.store(true);
            SaveInputSettings();
        }

        bool CanUseGameplayBindings(bool allow_free_camera = false)
        {
            if (!CanUseHotkeys()) return false;
            const auto* controls = RE::ControlMap::GetSingleton();
            if (!controls) return false;
            const auto& data = controls->GetRuntimeData();
            // Other menus/plugins may own typing without opening a vanilla application menu.
            // Explicit Stop is also valid in TFC. Actor-action cancellation still uses gameplay only.
            return data.textEntryCount <= 0 && (data.contextPriorityStack.empty() ||
                data.contextPriorityStack.back() == RE::UserEvents::INPUT_CONTEXT_ID::kGameplay ||
                (allow_free_camera && data.contextPriorityStack.back() == RE::UserEvents::INPUT_CONTEXT_ID::kTFCMode));
        }

        std::optional<float> SkyrimFloat(std::string_view name)
        {
            auto* preferences = RE::INIPrefSettingCollection::GetSingleton();
            auto* general = RE::INISettingCollection::GetSingleton();
            auto* setting = preferences ? preferences->GetSetting(name) : nullptr;
            if (!setting && general) setting = general->GetSetting(name);
            if (!setting || setting->GetType() != RE::Setting::Type::kFloat) return {};
            return setting->GetFloat();
        }

        void RefreshSensitivityLimits()
        {
            const auto minimum = SkyrimFloat("fMouseHeadingSensitivityMin:Controls");
            const auto maximum = SkyrimFloat("fMouseHeadingSensitivityMax:Controls");
            const auto current = SkyrimFloat("fMouseHeadingSensitivity:Controls");
            const bool valid = minimum && maximum && current && std::isfinite(*minimum) &&
                std::isfinite(*maximum) && std::isfinite(*current) && *minimum > 0.0F &&
                *maximum > *minimum && *current > 0.0F && (*minimum / *current) * 0.5F > 0.0F &&
                std::isfinite((*maximum / *current) * 3.0F);
            if (valid) {
                // Our 1.0 means the player's current native setting. Express Skyrim's
                // minimum/maximum relative to it, then extend the slower end by 2x
                // and the faster end by 3x without changing the player's preferences.
                const auto lower = (*minimum / *current) * 0.5F;
                const auto upper = (*maximum / *current) * 3.0F;
                sensitivity_min.store(lower);
                sensitivity_max.store(upper);
                // Manual INI edits can exceed the current range. Restore the default
                // instead of leaving the cursor at an extreme endpoint.
                const auto value = mouse_sensitivity.load();
                if (value != 1.0F && (value < lower || value > upper)) {
                    mouse_sensitivity.store(1.0F);
                    settings_dirty.store(true);
                    SKSE::log::warn("MouseSensitivity {} is outside {}-{}. Resetting to 1.0.", value, lower, upper);
                    SaveInputSettings();
                }
            }
            sensitivity_limits_available.store(valid);
        }

        Event MoveMenuCursor(RE::MouseMoveEvent* motion)
        {
            auto* ui = RE::UI::GetSingleton();
            auto menu = ui ? ui->GetMenu<RE::CursorMenu>() : nullptr;
            auto* cursor = RE::MenuCursor::GetSingleton();
            if (menu && cursor) {
                auto& data = cursor->GetRuntimeData();
                if (data.screenWidthX > 0.0F && data.screenWidthY > 0.0F) {
                    const auto scaled = [](int count, double& remainder) {
                        const auto amount = count * static_cast<double>(mouse_sensitivity.load()) + remainder;
                        const auto whole = static_cast<int>(std::clamp(amount,
                            static_cast<double>(std::numeric_limits<int>::min()), static_cast<double>(std::numeric_limits<int>::max())));
                        remainder = amount - whole;
                        return whole;
                    };
                    const auto x = motion->mouseInputX, y = motion->mouseInputY;
                    motion->mouseInputX = scaled(x, mouse_remainder_x);
                    motion->mouseInputY = scaled(y, mouse_remainder_y);
                    // Our sink stops gameplay dispatch, so the native cursor would not see
                    // this event. Run only its handler once, then restore the original counts.
                    menu->AsMenuEventHandler()->ProcessMouseMove(motion);
                    motion->mouseInputX = x;
                    motion->mouseInputY = y;
                    return {Event::Position, 0, false, data.cursorPosX / data.screenWidthX, data.cursorPosY / data.screenWidthY};
                }
            }
            static bool warned = false;
            if (!warned) SKSE::log::warn("Native menu cursor is unavailable. Using normalized raw mouse input.");
            warned = true;
            return {Event::Motion, 0, false, static_cast<float>(motion->mouseInputX), static_cast<float>(motion->mouseInputY)};
        }

        std::vector<std::uint32_t> InputContexts(const RE::ControlMap& controls)
        {
            std::vector<std::uint32_t> contexts;
            for (const auto context : controls.GetRuntimeData().contextPriorityStack) {
                contexts.push_back(static_cast<std::uint32_t>(context));
            }
            return contexts;
        }

        ContextPosition CapturedContextPosition()
        {
            const auto* controls = RE::ControlMap::GetSingleton();
            if (!controls || controls != captured_controls) return ContextPosition::Replaced;
            return LocateInputContext(captured_context, InputContexts(*controls));
        }

        void ReleaseCursorContext()
        {
            if (captured_context.empty()) return;
            const auto position = CapturedContextPosition();
            if (position == ContextPosition::Covered) {
                context_cleanup_pending.store(true);
                return;
            }
            if (position == ContextPosition::Current) {
                captured_controls->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
            } else {
                SKSE::log::debug("Input context stack replaced. No cursor reference removed");
            }
            captured_context.clear();
            captured_controls = nullptr;
            context_cleanup_pending.store(false);
        }

        void CaptureGameControls(bool capture)
        {
            auto* controls = RE::ControlMap::GetSingleton();
            using Flag = RE::UserEvents::USER_EVENT_FLAG;
            if (capture) {
                // Plugins can inspect input before this sink sees it. A non-gameplay context
                // tells those plugins that another menu owns keys, including search typing.
                controls->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
                captured_controls = controls;
                captured_context = InputContexts(*controls);
                disabled_controls = 0;
                for (const auto flag : {Flag::kMovement, Flag::kLooking, Flag::kFighting, Flag::kActivate,
                         Flag::kJumping, Flag::kSneaking, Flag::kPOVSwitch, Flag::kWheelZoom}) {
                    if (controls->AreControlsEnabled(flag)) {
                        disabled_controls |= static_cast<std::uint32_t>(flag);
                    }
                }
                controls->ToggleControls(static_cast<Flag>(disabled_controls), false, false);
                if (auto* player_controls = RE::PlayerControls::GetSingleton()) {
                    player_controls->data.moveInputVec = {0.0F, 0.0F};
                    player_controls->data.prevMoveVec = {0.0F, 0.0F};
                    player_controls->data.autoMove = false;
                    // Release actions held before capture because their key-up will be swallowed.
                    for (auto* handler : player_controls->heldStateHandlers) {
                        if (handler) {
                            handler->SetHeldStateActive(false);
                        }
                    }
                    player_controls->data.setupHeldStatesForRelease = true;
                }
            } else {
                ReleaseCursorContext();
                if (controls && disabled_controls) {
                    controls->ToggleControls(static_cast<Flag>(disabled_controls), true, false);
                }
                disabled_controls = 0;
            }
            CaptureMenuCamera(capture);
        }

        bool ChangeCapture(InputMode next, std::string_view reason)
        {
            const auto previous = capture_mode.load();
            if (next == previous) return true;
            auto* controls = RE::ControlMap::GetSingleton();
            if (next != InputMode::None && (!controls || context_cleanup_pending.load())) return false;
            if (next == InputMode::Menu && !CanOpen()) return false;
            if (next == InputMode::Quick && (previous != InputMode::None || !CanUseGameplayBindings(true))) return false;

            // This is the only writer of capture mode. All engine acquisition and
            // release runs here on the game thread, including render-originated closes.
            capture_mode.store(InputMode::None);
            if (previous == InputMode::Menu) {
                key_capture.store(KeyCapture::None);
                binding_conflict.store(false);
                SaveInputSettings();
                // Balance our text-input reference on the ControlMap that acquired it.
                if (controls && controls == captured_controls) controls->AllowTextInput(false);
            }
            if (previous != InputMode::None) CaptureGameControls(false);
            text_input.store(false);
            {
                std::lock_guard lock(input_mutex);
                pending.clear();
                reset_input = true;
            }

            // A foreign context can defer release of our old cursor reference.
            // Do not acquire another reference until that cleanup finishes.
            if (next != InputMode::None && context_cleanup_pending.load()) return false;
            if (next == InputMode::Menu) {
                RefreshSensitivityLimits();
                mouse_remainder_x = mouse_remainder_y = 0.0;
                if (auto* cursor = RE::MenuCursor::GetSingleton()) {
                    auto& data = cursor->GetRuntimeData();
                    data.cursorPosX = data.screenWidthX * 0.5F;
                    data.cursorPosY = data.screenWidthY * 0.5F;
                }
            } else if (next == InputMode::Quick) {
                quick_started = std::chrono::steady_clock::now();
            }
            if (next != InputMode::None) CaptureGameControls(true);
            // Skyrim emits CharEvent only while text input is allowed.
            if (next == InputMode::Menu) controls->AllowTextInput(true);
            capture_mode.store(next);
            SKSE::log::info("Input capture: {} -> {}, reason={}",
                static_cast<unsigned>(previous), static_cast<unsigned>(next), reason);
            return true;
        }

        void UpdateInput()
        {
            if (context_cleanup_pending.load()) ReleaseCursorContext();
            const auto mode = capture_mode.load();
            const auto focused = HasFocus();
            if (!focused) owned_buttons.clear();
            if (mode == InputMode::None) return;
            const auto* controls = RE::ControlMap::GetSingleton();
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - quick_started).count();
            const auto exit = CheckCapture(mode, {focused, CanUseHotkeys(), renderer_available.load(),
                mode == InputMode::Quick ? CapturedContextPosition() : ContextPosition::Current,
                controls && controls->GetRuntimeData().textEntryCount > 0, elapsed >= quick_timeout_seconds});
            switch (exit) {
            case CaptureExit::None: HoldMenuCamera(); break;
            case CaptureExit::FocusLost: ChangeCapture(InputMode::None, "game lost focus"); break;
            case CaptureExit::GameUnavailable: ChangeCapture(InputMode::None, "game unavailable"); break;
            case CaptureExit::RendererUnavailable: ChangeCapture(InputMode::None, "renderer unavailable"); break;
            case CaptureExit::InputTaken: ChangeCapture(InputMode::None, "another input context or text entry took over"); break;
            case CaptureExit::Timeout: ChangeCapture(InputMode::None, "Quick Emote timeout"); break;
            }
        }

        ImGuiKey Key(unsigned scan)
        {
            constexpr std::array<unsigned, 26> letters{30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
            for (unsigned i = 0; i < letters.size(); ++i) {
                if (scan == letters[i]) return static_cast<ImGuiKey>(ImGuiKey_A + i);
            }
            if (scan >= 2 && scan <= 10) return static_cast<ImGuiKey>(ImGuiKey_1 + scan - 2);
            switch (scan) {
            case 11: return ImGuiKey_0;
            case 12: return ImGuiKey_Minus;
            case 13: return ImGuiKey_Equal;
            case 26: return ImGuiKey_LeftBracket;
            case 27: return ImGuiKey_RightBracket;
            case 39: return ImGuiKey_Semicolon;
            case 40: return ImGuiKey_Apostrophe;
            case 41: return ImGuiKey_GraveAccent;
            case 43: return ImGuiKey_Backslash;
            case 51: return ImGuiKey_Comma;
            case 52: return ImGuiKey_Period;
            case 53: return ImGuiKey_Slash;
            case 29: return ImGuiKey_LeftCtrl;
            case 157: return ImGuiKey_RightCtrl;
            case 42: return ImGuiKey_LeftShift;
            case 54: return ImGuiKey_RightShift;
            case 56: return ImGuiKey_LeftAlt;
            case 184: return ImGuiKey_RightAlt;
            case 199: return ImGuiKey_Home;
            case 207: return ImGuiKey_End;
            case 211: return ImGuiKey_Delete;
            case 201: return ImGuiKey_PageUp;
            case 209: return ImGuiKey_PageDown;
            case 14: return ImGuiKey_Backspace;
            case 15: return ImGuiKey_Tab;
            case 28: return ImGuiKey_Enter;
            case 57: return ImGuiKey_Space;
            case 200: return ImGuiKey_UpArrow;
            case 203: return ImGuiKey_LeftArrow;
            case 205: return ImGuiKey_RightArrow;
            case 208: return ImGuiKey_DownArrow;
            default: return ImGuiKey_None;
            }
        }

        template<class T>
        std::optional<T> ReadInputNumber(const wchar_t* name, const wchar_t* fallback,
            T (*parse)(const wchar_t*, wchar_t**))
        {
            wchar_t value[32]{};
            const auto length = GetPrivateProfileStringW(L"Input", name, fallback, value, 32, settings_file.c_str());
            wchar_t* end{};
            errno = 0;
            const auto number = parse(value, &end);
            if (length >= 31 || end == value || end != value + length || errno == ERANGE) return {};
            return number;
        }

        std::optional<unsigned> ReadInputKey(const wchar_t* name, const wchar_t* fallback)
        {
            const auto number = ReadInputNumber<unsigned long>(name, fallback,
                [](const wchar_t* value, wchar_t** end) { return std::wcstoul(value, end, 10); });
            if (!number || *number > 255) return {};
            return static_cast<unsigned>(*number);
        }

        InputBindings CurrentBindings()
        {
            return {menu_key.load(), stop_key.load(), quick_key.load()};
        }

        void BindKey(unsigned code, KeyCapture target)
        {
            if (target == KeyCapture::Favorite) {
                std::lock_guard lock(input_mutex);
                pending.push_back({Event::Shortcut, code});
                return;
            }
            const auto change = RebindInputKey(code, target, CurrentBindings());
            if (!change) return;
            binding_conflict.store(change->conflict);
            if (change->conflict) return;
            menu_key.store(change->keys.menu);
            stop_key.store(change->keys.stop);
            quick_key.store(change->keys.quick);
            key_capture.store(KeyCapture::None);
            settings_dirty.store(true);
            SaveInputSettings();
        }

        struct KeyHandling { bool consumed = false; bool binding_press = false; };

        KeyHandling HandleKeyPress(unsigned code, bool& rebound)
        {
            const auto binding = key_capture.load();
            const auto command = ChooseInputCommand(code, CurrentBindings(), {
                capture_mode.load(), binding, text_input.load(), CanOpen(), CanUseGameplayBindings(true)});
            switch (command) {
            case InputCommand::Bind:
                rebound = true;
                BindKey(code, binding);
                return {true, true};
            case InputCommand::QuickFavorite:
                if (PlayQuickFavoriteHotkey(code)) ChangeCapture(InputMode::None, "favorite selected");
                return {true, true};
            case InputCommand::EndQuick:
                ChangeCapture(InputMode::None, "Quick Emote Escape");
                return {true, true};
            case InputCommand::ToggleMenu:
                ChangeCapture(IsMenuOpen() ? InputMode::None : InputMode::Menu, "menu hotkey");
                return {true, false};
            case InputCommand::BeginQuick: {
                const auto started = ChangeCapture(InputMode::Quick, "Quick Emote hotkey");
                return {started, started};
            }
            case InputCommand::Stop:
                SKSE::log::debug("Stop hotkey accepted: scan={}", code);
                QueueStop();
                return {true, true};
            case InputCommand::CloseMenu:
                ChangeCapture(InputMode::None, "menu Escape");
                return {true, false};
            case InputCommand::None:
                return {};
            }
            return {};
        }

        void HandleGameplayAction(const RE::ButtonEvent& button)
        {
            // Console typing and free-camera controls do not move the actor.
            // Interrupt before Skyrim handles the action so a delayed reset cannot
            // interfere with the requested jump or weapon draw.
            const auto* camera = RE::PlayerCamera::GetSingleton();
            if (!button.IsDown() || !CanUseGameplayBindings() || (camera && camera->IsInFreeCameraMode())) return;
            std::string_view action = button.GetUserEvent().c_str();
            const auto device = button.GetDevice();
            // Skyrim may not have named this event before our sink. Resolve remapped keys.
            if (const auto* map = RE::ControlMap::GetSingleton(); action.empty() && map &&
                (device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse || device == RE::INPUT_DEVICE::kGamepad)) {
                action = map->GetUserEventName(button.GetIDCode(), device);
            }
            // Sprint alone does not move a stationary actor. Movement still cancels.
            if (action == "Forward" || action == "Back" || action == "Strafe Left" ||
                action == "Strafe Right" || action == "Jump" || action == "Ready Weapon" ||
                action == "Activate" || action == "Left Attack/Block" ||
                action == "Right Attack/Block" || action == "Shout" || action == "Sneak" ||
                action == "Auto-Move") {
                InterruptPlayback();
            }
        }

        void QueueMenuButton(const RE::ButtonEvent& button)
        {
            if (!IsMenuOpen()) return;
            const auto code = button.GetIDCode();
            const bool keyboard = button.GetDevice() == RE::INPUT_DEVICE::kKeyboard;
            const bool mouse = button.GetDevice() == RE::INPUT_DEVICE::kMouse;
            if (keyboard && ((code == menu_key.load() && !text_input.load()) || code == 1)) return;
            std::lock_guard lock(input_mutex);
            if (keyboard) {
                if (button.IsDown() && !text_input.load()) pending.push_back({Event::Shortcut, code});
                pending.push_back({Event::Key, code, button.IsPressed()});
            } else if (mouse && code < 5) {
                pending.push_back({Event::Mouse, code, button.IsPressed()});
            } else if (mouse && (code == 8 || code == 9) && button.IsPressed()) {
                pending.push_back({Event::Wheel, code});
            }
        }

        class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
        {
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
                RE::BSTEventSource<RE::InputEvent*>*) override
            {
                CheckInitialGameplay();
                static bool first_callback = true;
                if (first_callback) SKSE::log::info("Input listener received its first callback");
                first_callback = false;
                UpdateInput();
                bool capture = capture_mode.load() != InputMode::None || !owned_buttons.empty();
                bool rebound = false;
                for (auto* event = events ? *events : nullptr; event; event = event->next) {
                    // Recheck before interpreting each key. Capture can change within a batch.
                    if (capture_mode.load() == InputMode::Quick) UpdateInput();
                    if (auto* button = event->AsButtonEvent()) {
                        const auto code = button->GetIDCode();
                        const bool keyboard = button->GetDevice() == RE::INPUT_DEVICE::kKeyboard;
                        const bool mouse = button->GetDevice() == RE::INPUT_DEVICE::kMouse;
                        const auto identity = (std::uint64_t(button->GetDevice()) << 32) | code;
                        bool consume = capture_mode.load() != InputMode::None || owned_buttons.contains(identity);
                        bool binding_press = false;
                        if (keyboard && button->IsDown()) {
                            static bool first_keyboard = true;
                            if (first_keyboard) SKSE::log::info("Input listener received a keyboard press");
                            first_keyboard = false;
                            if (code == menu_key.load()) {
                                auto* player = RE::PlayerCharacter::GetSingleton();
                                auto* ui = RE::UI::GetSingleton();
                                SKSE::log::info("Menu key received: scan={} open={} canOpen={} session={} readySession={} renderer={} focus={} player={} loaded3D={} dead={} ui={} paused={} applicationMenu={} textInput={} keyCapture={}",
                                    code, IsMenuOpen(), CanOpen(), input_session_id.load(), ready_session_id.load(),
                                    renderer_available.load(), HasFocus(), player != nullptr, player && player->Is3DLoaded(),
                                    player && player->IsDead(), ui != nullptr, ui && ui->GameIsPaused(),
                                    ui && ui->IsApplicationMenuOpen(), text_input.load(), static_cast<unsigned>(key_capture.load()));
                            }
                            const auto handled = HandleKeyPress(code, rebound);
                            consume |= handled.consumed;
                            binding_press = handled.binding_press;
                        }
                        if (consume) {
                            capture = true;
                            // Wheel ticks have no matching release event.
                            if (!mouse || code < 5) {
                                if (button->IsPressed()) {
                                    owned_buttons.insert(identity);
                                } else {
                                    owned_buttons.erase(identity);
                                }
                            }
                        }
                        if (!capture) HandleGameplayAction(*button);
                        if (!binding_press) QueueMenuButton(*button);
                    } else if (IsMenuOpen()) {
                        if (auto* motion = event->AsMouseMoveEvent()) {
                            const auto position = MoveMenuCursor(motion);
                            std::lock_guard lock(input_mutex);
                            pending.push_back(position);
                        } else if (const auto* character = event->AsCharEvent(); character && !rebound && key_capture.load() == KeyCapture::None) {
                            std::lock_guard lock(input_mutex);
                            pending.push_back({Event::Character, character->keyCode});
                        }
                    }
                }
                return capture ? RE::BSEventNotifyControl::kStop : RE::BSEventNotifyControl::kContinue;
            }
        } sink;
    }

    bool IsMenuOpen()
    {
        return capture_mode.load() == InputMode::Menu;
    }

    InputSettings GetInputSettings()
    {
        std::lock_guard lock(input_mutex);
        const auto binding = key_capture.load();
        return {
            .menu_key = menu_key.load(),
            .mouse_sensitivity = mouse_sensitivity.load(),
            .capturing_key = binding == KeyCapture::Menu,
            .save_error = settings_error,
            .sensitivity_min = sensitivity_min.load(),
            .sensitivity_max = sensitivity_max.load(),
            .sensitivity_limits_available = sensitivity_limits_available.load(),
            .stop_key = stop_key.load(),
            .capturing_stop_key = binding == KeyCapture::Stop,
            .binding_conflict = binding_conflict.load(),
            .quick_emote_key = quick_key.load(),
            .capturing_quick_emote_key = binding == KeyCapture::Quick,
            .quick_emote_timeout = quick_timeout_seconds};
    }

    std::string MenuKeyLabel(unsigned scan_code)
    {
        if (!scan_code) return "Unbound";
        // DirectInput marks extended keys with bit 7. Win32 expects that flag in bit 24.
        const auto key = static_cast<LONG>(((scan_code & 0x7f) << 16) | ((scan_code & 0x80) ? 1 << 24 : 0));
        wchar_t name[64]{};
        const auto length = GetKeyNameTextW(key, name, 64);
        char utf8[256]{};
        if (length && WideCharToMultiByte(CP_UTF8, 0, name, length, utf8, sizeof(utf8), nullptr, nullptr)) return utf8;
        return "Scan code " + std::to_string(scan_code);
    }

    void SetMenuMouseSensitivity(float value)
    {
        if (std::isfinite(value) && value > 0.0F) {
            if (sensitivity_limits_available.load()) value = std::clamp(value, sensitivity_min.load(), sensitivity_max.load());
            if (mouse_sensitivity.exchange(value) != value) settings_dirty.store(true);
        }
    }

    void CaptureMenuKey(bool active)
    {
        CaptureKey(KeyCapture::Menu, active);
    }

    void CaptureStopKey(bool active)
    {
        CaptureKey(KeyCapture::Stop, active);
    }

    void CaptureQuickEmoteKey(bool active)
    {
        CaptureKey(KeyCapture::Quick, active);
    }

    void ClearQuickEmoteKey()
    {
        ClearKey(quick_key);
    }

    void ClearStopKey()
    {
        ClearKey(stop_key);
    }

    void CaptureFavoriteKey(bool active)
    {
        if (active && IsMenuOpen()) key_capture.store(KeyCapture::Favorite);
        else {
            auto expected = KeyCapture::Favorite;
            key_capture.compare_exchange_strong(expected, KeyCapture::None);
        }
    }

    bool IsCapturingFavoriteKey()
    {
        return key_capture.load() == KeyCapture::Favorite;
    }

    void CloseMenu()
    {
        // Restore game controls and camera ownership on the same thread that captured them.
        const auto session = input_session_id.load();
        if (const auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([session] {
            if (session == input_session_id.load() && IsMenuOpen()) ChangeCapture(InputMode::None, "menu command");
        });
    }

    void ResetInputSettings()
    {
        key_capture.store(KeyCapture::None);
        menu_key.store(26);
        stop_key.store(0);
        quick_key.store(0);
        binding_conflict.store(false);
        mouse_sensitivity.store(1.0F);
        settings_dirty.store(true);
        SaveInputSettings();
    }

    void SaveInputSettings()
    {
        if (!settings_dirty.load()) return;
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) return;
        // INI writes run on the game thread, never once per slider frame. Each task
        // saves the latest settings and preserves unrelated INI keys and sections.
        tasks->AddTask([] {
            if (!settings_dirty.exchange(false)) return;
            const auto key = std::to_wstring(menu_key.load());
            const auto sensitivity = std::to_wstring(mouse_sensitivity.load());
            const auto stop = std::to_wstring(stop_key.load());
            const auto quick = std::to_wstring(quick_key.load());
            const bool saved_key = WritePrivateProfileStringW(L"Input", L"MenuKey", key.c_str(), settings_file.c_str());
            const auto key_error = saved_key ? ERROR_SUCCESS : GetLastError();
            const bool saved_stop = WritePrivateProfileStringW(L"Input", L"StopKey", stop.c_str(), settings_file.c_str());
            const auto stop_error = saved_stop ? ERROR_SUCCESS : GetLastError();
            const bool saved_quick = WritePrivateProfileStringW(L"Input", L"QuickEmoteKey", quick.c_str(), settings_file.c_str());
            const auto quick_error = saved_quick ? ERROR_SUCCESS : GetLastError();
            const bool saved_mouse = WritePrivateProfileStringW(L"Input", L"MouseSensitivity", sensitivity.c_str(), settings_file.c_str());
            const auto mouse_error = saved_mouse ? ERROR_SUCCESS : GetLastError();
            std::string error;
            if (!saved_key || !saved_stop || !saved_quick || !saved_mouse) {
                error = "Cannot save input settings. Changes apply until Skyrim closes.";
                SKSE::log::error("{} File: {}. MenuKey error={}, StopKey error={}, QuickEmoteKey error={}, MouseSensitivity error={}",
                    error, settings_file.string(), key_error, stop_error, quick_error, mouse_error);
                settings_dirty.store(true);
            }
            std::lock_guard lock(input_mutex);
            settings_error = error;
        });
    }

    bool InstallInput(const std::filesystem::path& ini)
    {
        if (installed) {
            return true;
        }
        settings_file = ini;
        SKSE::log::info("Input configuration: path={} exists={}", ini.string(), GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES);
        const auto menu = ReadInputKey(L"MenuKey", L"26");
        if (menu && ValidInputBinding(*menu, KeyCapture::Menu, {})) menu_key.store(*menu);
        else SKSE::log::warn("Invalid MenuKey in SkyEmote.ini. Using scan code 26 ([).");
        const auto stop = ReadInputKey(L"StopKey", L"0");
        if (stop && ValidInputBinding(*stop, KeyCapture::Stop, CurrentBindings())) stop_key.store(*stop);
        else SKSE::log::warn("Invalid or conflicting StopKey in SkyEmote.ini. Leaving Stop unbound.");
        const auto quick = ReadInputKey(L"QuickEmoteKey", L"0");
        if (quick && ValidInputBinding(*quick, KeyCapture::Quick, CurrentBindings())) quick_key.store(*quick);
        else SKSE::log::warn("Invalid or conflicting QuickEmoteKey. Leaving Quick Emote unbound.");
        const auto timeout = ReadInputNumber<double>(L"QuickEmoteTimeoutSeconds", L"5", std::wcstod);
        if (timeout && std::isfinite(*timeout) && *timeout > 0.0) quick_timeout_seconds = *timeout;
        else SKSE::log::warn("Invalid QuickEmoteTimeoutSeconds. Using 5 seconds.");
        const auto sensitivity = ReadInputNumber<float>(L"MouseSensitivity", L"1.0", std::wcstof);
        if (sensitivity && std::isfinite(*sensitivity) && *sensitivity > 0.0F) mouse_sensitivity.store(*sensitivity);
        else SKSE::log::warn("Invalid MouseSensitivity in SkyEmote.ini. Using 1.0.");
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (!input) {
            SKSE::log::error("Skyrim input manager is unavailable");
            return false;
        }
        input->PrependEventSink(&sink);
        installed = true;
        SKSE::log::info("Menu input installed. MenuKey={}, StopKey={}, QuickEmoteKey={}, QuickEmoteTimeoutSeconds={}, MouseSensitivity={}",
            menu_key.load(), stop_key.load(), quick_key.load(), quick_timeout_seconds, mouse_sensitivity.load());
        return true;
    }

    void SetInputReady(bool value)
    {
        const auto session = ++input_session_id;
        SKSE::log::info("Input readiness requested: session={} ready={}", session, value);
        if (const auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([value, session] {
                // A newer load notification replaces an older queued readiness change.
                if (session != input_session_id.load()) {
                    SKSE::log::debug("Input readiness discarded: session={} current={}", session, input_session_id.load());
                    return;
                }
                ChangeCapture(InputMode::None, "load/readiness change");
                owned_buttons.clear();
                if (value) {
                    ready_session_id.store(session);
                    SKSE::log::info("Input session ready: {}", session);
                }
            });
        } else {
            SKSE::log::error("Cannot update input readiness: SKSE task interface unavailable");
        }
    }

    void QueueInputUpdate(bool renderer_ready)
    {
        if (renderer_available.exchange(renderer_ready) != renderer_ready) {
            SKSE::log::info("Input renderer availability: {}", renderer_ready);
        }
        if ((capture_mode.load() == InputMode::None && !context_cleanup_pending.load()) || update_pending.exchange(true)) {
            return;
        }
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            update_pending.store(false);
            return;
        }
        tasks->AddTask([] {
            update_pending.store(false);
            UpdateInput();
        });
    }

    void SetMenuTextInput(bool active)
    {
        text_input.store(active && IsMenuOpen());
    }

    void FeedMenuInput()
    {
        auto& io = ImGui::GetIO();
        std::vector<Event> events;
        bool reset;
        {
            std::lock_guard lock(input_mutex);
            events.swap(pending);
            reset = std::exchange(reset_input, false);
        }
        // Callbacks below can save favorites and read settings. Release the input lock
        // before invoking them so input collection and error reads cannot deadlock.
        static ImVec2 cursor{};
        static std::array<bool, 256> keys{};
        io.MouseDrawCursor = true;
        if (reset || io.MousePos.x < 0.0F) {
            io.ClearEventsQueue();
            io.ClearInputKeys();
            keys.fill(false);
            cursor = ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F);
            io.AddMousePosEvent(cursor.x, cursor.y);
        }
        // Scale both axes equally by display height. Crossing a 4K menu should take
        // the same mouse travel as crossing a 1080p menu with the same aspect ratio.
        const auto mouse_scale = mouse_sensitivity.load() * io.DisplaySize.y / 1080.0F;
        bool shortcut_played = false;
        for (const auto& event : events) {
            switch (event.kind) {
            case Event::Shortcut:
                if (!shortcut_played && HandleFavoriteHotkey(event.code)) shortcut_played = true;
                break;
            case Event::Key:
                // A handled shortcut must not also activate a focused ImGui button with Enter/Space.
                if (shortcut_played && event.down) break;
                if (event.code < keys.size()) keys[event.code] = event.down;
                io.AddKeyEvent(ImGuiMod_Ctrl, keys[29] || keys[157]);
                io.AddKeyEvent(ImGuiMod_Shift, keys[42] || keys[54]);
                io.AddKeyEvent(ImGuiMod_Alt, keys[56] || keys[184]);
                if (const auto key = Key(event.code); key != ImGuiKey_None) {
                    io.AddKeyEvent(key, event.down);
                }
                break;
            case Event::Character:
                if (!shortcut_played) io.AddInputCharacter(event.code);
                break;
            case Event::Mouse: io.AddMouseButtonEvent(event.code, event.down); break;
            case Event::Wheel: io.AddMouseWheelEvent(0.0F, event.code == 8 ? 1.0F : -1.0F); break;
            case Event::Position:
                cursor.x = std::clamp(event.x * io.DisplaySize.x, 0.0F, std::max(0.0F, io.DisplaySize.x - 1.0F));
                cursor.y = std::clamp(event.y * io.DisplaySize.y, 0.0F, std::max(0.0F, io.DisplaySize.y - 1.0F));
                io.AddMousePosEvent(cursor.x, cursor.y);
                break;
            case Event::Motion:
                cursor.x = std::clamp(cursor.x + event.x * mouse_scale, 0.0F, std::max(0.0F, io.DisplaySize.x - 1.0F));
                cursor.y = std::clamp(cursor.y + event.y * mouse_scale, 0.0F, std::max(0.0F, io.DisplaySize.y - 1.0F));
                io.AddMousePosEvent(cursor.x, cursor.y);
                break;
            }
        }
    }
}
