// SPDX-License-Identifier: MIT
#include <SKSE/SKSE.h>

#include "core/Pack.h"
#include "core/Playback.h"
#include "core/Renderer.h"
#include "core/Input.h"
#include "core/Menu.h"
#include "core/PackWarnings.h"
#include "core/Startup.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/U/UI.h>
#include <mutex>
#include <stdexcept>
#include <spdlog/sinks/rotating_file_sink.h>

namespace
{
    // Only initial console entry can use the fallback. Any explicit load/new-game
    // notification takes ownership, even if loading fails and old player 3D survives.
    SkyEmote::InitialGameplay initial_gameplay;
    std::mutex startup_mutex;

    void SetGameplayReady(bool ready, std::string_view reason)
    {
        SKSE::log::info("Gameplay readiness: ready={} reason={}", ready, reason);
        SkyEmote::SetPlaybackReady(ready);
        SkyEmote::SetPackWarningsReady(ready);
        SkyEmote::SetInputReady(ready);
    }

    void LoadGameData()
    {
        const auto game = std::filesystem::path(REL::Module::get().filePath().data()).parent_path();
        SKSE::log::info("Game directory: {}", game.string());
        if (!SkyEmote::InstallInput(game / L"Data/SKSE/Plugins/SkyEmote.ini")) {
            SKSE::log::error("Input installation failed. Menu hotkeys will be unavailable");
        }
        SkyEmote::ReloadPacks(game / L"Data/SKSE/Plugins/SkyEmote/Packs");
        const auto old_directory = SKSE::log::log_directory();
        SkyEmote::LoadMenuFavorites(game / L"Data/SKSE/Plugins/SkyEmote.favorites.json",
            old_directory ? *old_directory / L"SkyEmote/favorites.json" : std::filesystem::path{});
        SkyEmote::LoadPackWarnings(game / L"Data");
        const auto& catalogue = SkyEmote::GetPacks();
        for (const auto& error : catalogue.errors) {
            SKSE::log::warn("Pack {} [{}]: {}", error.file, error.location, error.reason);
        }
        SKSE::log::info("Loaded {} packs; {} pack errors", catalogue.packs.size(), catalogue.errors.size());
        for (const auto& pack : catalogue.packs) {
            std::size_t emotes = 0;
            for (const auto& category : pack.categories) {
                for (const auto& section : category.sections) emotes += section.emotes.size();
            }
            SKSE::log::info("Pack loaded: id={} categories={} emotes={} graphRequirement={}",
                pack.id, pack.categories.size(), emotes, pack.graph.has_value());
        }
    }
}

void SkyEmote::CheckInitialGameplay()
{
    // Serialize promotion with load notifications so a concurrently starting load
    // always invalidates this fallback before it can enable a stale session.
    std::lock_guard lock(startup_mutex);
    if (!initial_gameplay.Waiting()) return;
    const auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    if (!player || !player->Is3DLoaded() || !player->GetParentCell() || player->IsDead() ||
        !ui || ui->GameIsPaused() || ui->IsApplicationMenuOpen() ||
        ui->IsMenuOpen("Main Menu") || ui->IsMenuOpen("Loading Menu")) return;

    if (!initial_gameplay.Enter()) return;
    SetGameplayReady(true, "initial gameplay detected without a save-load/NewGame notification");
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    if (skse->IsEditor() || skse->RuntimeVersion() < SKSE::RUNTIME_SSE_1_5_97) {
        return false;
    }

    // Keep previous launches as well as bounding this run's diagnostics. CommonLib's
    // logRotate rotates on launch but has no per-file size setting, so replace its sink.
    SKSE::Init(skse, {.log = false});
    try {
        const auto directory = SKSE::log::log_directory();
        if (!directory) throw std::runtime_error("SKSE log directory unavailable");
        const auto path = *directory / "SkyEmote.log";
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.string(), 2 * 1024 * 1024, 3, true);
        auto logger = std::make_shared<spdlog::logger>("SkyEmote", sink);
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%T.%e] [%=5t] [%L] %v");
        SKSE::log::info("Log file: {} (2 MiB, 3 backups, rotates on launch)", path.string());
    } catch (const std::exception& error) {
        SKSE::log::error("Cannot configure SkyEmote file logging: {}", error.what());
    }
    SKSE::log::info("SkyEmote {}: Skyrim={} SKSE={:#x}", SKSE::GetPluginVersion().string(),
        skse->RuntimeVersion().string(), skse->SKSEVersion());
    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
            SKSE::log::info("SKSE lifecycle message: type={} dataPresent={} length={}",
                message->type, message->data != nullptr, message->dataLen);
            std::lock_guard lock(startup_mutex);
            if (message->type == SKSE::MessagingInterface::kDataLoaded) {
                LoadGameData();
                if (!SkyEmote::InstallRenderer()) SKSE::log::error("Renderer installation failed. Menu drawing will be unavailable");
                initial_gameplay.DataLoaded();
                SKSE::log::info("Initial gameplay fallback armed={}", initial_gameplay.Waiting());
            } else if (message->type == SKSE::MessagingInterface::kPreLoadGame) {
                initial_gameplay.UseLoadLifecycle();
                SetGameplayReady(false, "PreLoadGame");
            } else if (message->type == SKSE::MessagingInterface::kPostLoadGame ||
                       message->type == SKSE::MessagingInterface::kNewGame) {
                // SKSE encodes post-load success in the data pointer itself.
                const bool loaded = message->type == SKSE::MessagingInterface::kNewGame || message->data != nullptr;
                initial_gameplay.UseLoadLifecycle();
                SetGameplayReady(loaded, message->type == SKSE::MessagingInterface::kNewGame ? "NewGame" : "PostLoadGame");
            }
        })) {
        SKSE::log::error("Cannot register the game lifecycle listener");
        return false;
    }
    SKSE::log::info("SkyEmote {} loaded", SKSE::GetPluginVersion().string());
    return true;
}
