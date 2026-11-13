// SPDX-License-Identifier: MIT
#include "MenuCamera.h"

#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerControls.h>
#include <RE/T/ThirdPersonState.h>
#include <algorithm>
#include <optional>

namespace SkyEmote
{
    namespace
    {
        struct CameraSettings
        {
            bool allow_auto_vanity;
            float idle_timer;
        };

        // Saved on opening and restored on closing. Access only on the game's main thread.
        std::optional<CameraSettings> captured;

        void ClearLook()
        {
            // Blocking mouse events also blocks their neutral updates. Clear cached deltas
            // so Skyrim cannot keep applying the last movement received before opening.
            if (auto* controls = RE::PlayerControls::GetSingleton()) {
                controls->data.lookInputVec = {0.0F, 0.0F};
                controls->data.prevLookVec = {0.0F, 0.0F};
            }
            if (auto* camera = RE::PlayerCamera::GetSingleton()) {
                camera->rotationInput.x = 0.0F;
                camera->rotationInput.y = 0.0F;
            }
        }
    }

    void HoldMenuCamera()
    {
        if (!captured) {
            return;
        }
        ClearLook();
        if (auto* camera = RE::PlayerCamera::GetSingleton()) {
            auto& data = camera->GetRuntimeData2();
            data.allowAutoVanityMode = false;
            data.idleTimer = std::max(captured->idle_timer, 1.0F);
            if (camera->IsCurrentState(RE::CameraState::kAutoVanity, RE::CameraState::kAutoVanity)) {
                camera->ForceThirdPerson();
            }
            if (camera->IsCurrentState(RE::CameraState::kThirdPerson, RE::CameraState::kVRThirdPerson)) {
                auto* third = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
                third->targetYaw = third->currentYaw;
            }
        }
    }

    void CaptureMenuCamera(bool open)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (open) {
            if (!captured && camera) {
                const auto& data = camera->GetRuntimeData2();
                captured = CameraSettings{data.allowAutoVanityMode, data.idleTimer};
            }
            HoldMenuCamera();
        } else {
            ClearLook();
            if (captured && camera) {
                auto& data = camera->GetRuntimeData2();
                data.allowAutoVanityMode = captured->allow_auto_vanity;
                data.idleTimer = captured->idle_timer;
            }
            captured.reset();
        }
    }
}
