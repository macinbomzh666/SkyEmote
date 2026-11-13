// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace SkyEmote
{
    struct InputSettings
    {
        unsigned menu_key;
        float mouse_sensitivity;
        bool capturing_key;
        std::string save_error;
        float sensitivity_min;
        float sensitivity_max;
        bool sensitivity_limits_available;
        unsigned stop_key;
        bool capturing_stop_key;
        bool binding_conflict;
        unsigned quick_emote_key;
        bool capturing_quick_emote_key;
        double quick_emote_timeout;
    };

    InputSettings GetInputSettings();
    std::string MenuKeyLabel(unsigned scan_code);
    void SetMenuMouseSensitivity(float value);
    void CaptureMenuKey(bool active);
    void CaptureStopKey(bool active);
    void ClearStopKey();
    void CaptureQuickEmoteKey(bool active);
    void ClearQuickEmoteKey();
    void CaptureFavoriteKey(bool active);
    bool IsCapturingFavoriteKey();
    bool IsMenuOpen();
    void CloseMenu();
    void ResetInputSettings();
    void SaveInputSettings();

    bool InstallInput(const std::filesystem::path& ini);
    void SetInputReady(bool ready);
    void QueueInputUpdate(bool renderer_ready);
    void FeedMenuInput();
    void SetMenuTextInput(bool active);
}
