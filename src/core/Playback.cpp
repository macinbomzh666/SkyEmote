// SPDX-License-Identifier: MIT
#include "Playback.h"
#include "StateSequence.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <RE/A/AIProcess.h>
#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/B/BSAnimationGraphManager.h>
// Typed TESForm lookups need FormTraits for the As<T> template definition.
#include <RE/F/FormTraits.h>
#include <RE/N/NiAVObject.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerCamera.h>
#include <RE/M/Main.h>
#include <RE/P/PlayerControls.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESIdleForm.h>
#include <RE/U/UI.h>
#include <SKSE/SKSE.h>

namespace SkyEmote
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // The menu can request commands and read results on a different thread from the game.
        // Therefore, we need the following atomics to share loading state and results safely,
        // and to discard queued commands when the player loads another character or a save
        // from a different point in time. A Wave queued before loading must not run in the new save.
        // Incremented at load start/completion/new game. Tasks retain the ID of their save session.
        std::atomic<std::uint64_t> session_id{0};
        // False before/during loading, true only after a successful load or new game.
        std::atomic<bool> ready{false};
        // Latest immutable message/error snapshot. The render thread can read it while tasks replace it.
        std::atomic<std::shared_ptr<const CommandResult>> billboard{
            std::make_shared<const CommandResult>()};

        // Main-thread playback publishes immutable state snapshots for the render thread.
        std::atomic<std::shared_ptr<const PlaybackStatus>> playback_status{
            std::make_shared<const PlaybackStatus>()};

        // Each Play, Stop or gameplay interruption replaces the previous user intent.
        // Queued commands remember this ID so rapid clicks cannot start an older selection later.
        std::atomic<std::uint64_t> request_id{0};
        // ID of the request currently preparing/playing, or zero when we own no playback.
        // Graph callbacks identify completion notices. Renderer-end schedules updates.
        std::atomic<std::uint64_t> active_id{0};
        // Render/load notifications set this when scheduling work. The main-thread task clears
        // it after updating, so a slow frame cannot accumulate duplicate playback updates.
        std::atomic<bool> update_pending{false};

        enum class Phase { WaitingForWeapon, Preparing, Sequence, Playing };

        struct Playback
        {
            std::string event, name;
            PlaybackOptions options;
            // Session identifies the save. Request identifies this selection's completion notices.
            std::uint64_t session, request;
            // Retain the third-person model that performs the emote, even in first person.
            // A rebuilt model invalidates ownership, but switching camera views must not.
            RE::NiPointer<RE::NiAVObject> root;
            // Independent emotes prepare after reset. Related state actions use Sequence directly.
            Phase phase = Phase::WaitingForWeapon;
            // Request sheathing once, only after an in-progress draw reaches Drawn.
            bool sheath_requested = false;
            // Set only after PlayIdle succeeds. Stop must release the engine's special-idle action too.
            bool native_idle_started = false;
            // The plan contains setup first and the requested main event last.
            std::vector<PlaybackStep> steps;
            std::size_t step_index = 0;
            bool step_started = false;
            std::string state;
            // While a step is rejected, elapsed is its bounded wait and retry_wait throttles attempts.
            // Once accepted, elapsed starts at zero and measures that step's actual entry.
            double retry_wait = 0.0;
            // Seconds in the current preparation, entry, retry wait or repeat period. Excludes pauses.
            double elapsed = 0.0;
            // Last observed camera/clock state, used to log transitions without per-frame messages.
            bool free_camera = false;
            bool clock_paused = false;
            Clock::time_point previous_tick = Clock::now();
            // Start acknowledgement time filters out late completion notices from the previous idle.
            Clock::time_point started{};
        };

        // Only main-thread tasks and the input callback may change playback state.
        std::optional<Playback> playback;
        struct Completion
        {
            std::uint64_t request;
            Clock::time_point when;
        };
        std::mutex completion_mutex;
        std::vector<Completion> completions;

        void PublishCommandResult(std::string message, bool error = false)
        {
            if (!message.empty()) {
                if (error) SKSE::log::warn("Playback: {}", message);
                else SKSE::log::info("Playback: {}", message);
            }
            billboard.store(std::make_shared<const CommandResult>(std::move(message), error));
        }

        void PublishStatus()
        {
            std::string state;
            bool transitioning = false;
            if (playback) {
                state = playback->state;
                transitioning = playback->phase != Phase::Playing ||
                    (!playback->options.requires_state.empty() && playback->options.state.empty());
                if (state.empty() && !transitioning && playback->options.mode == PlaybackMode::Loop) {
                    state = playback->name;
                }
            }
            playback_status.store(std::make_shared<const PlaybackStatus>(state, transitioning,
                playback ? playback->options.state_scope : ""));
        }

        void ForgetPlayback(bool publish_status = true)
        {
            active_id.store(0);
            playback.reset();
            if (publish_status) {
                PublishStatus();
            }
            std::lock_guard lock(completion_mutex);
            completions.clear();
        }

        bool ResetGraph(RE::PlayerCharacter* player, bool native_idle = false)
        {
            const auto weapon = player->AsActorState()->GetWeaponState();
            // Default-state resets can skip the weapon attachment/control events that
            // drawing and sheathing still need. Never force a reset in those transitions.
            if (weapon != RE::WEAPON_STATE::kSheathed && weapon != RE::WEAPON_STATE::kDrawn) return false;
            // Attempt every cleanup event even if an earlier one is rejected.
            bool accepted = false;
            if (native_idle) {
                // IdleStop_Loose exits the engine's special-idle action.
                auto* stop = RE::TESForm::LookupByID<RE::TESIdleForm>(0x0010D9EE);
                auto* process = player->GetActorRuntimeData().currentProcess;
                const bool stopped = stop && process && process->PlayIdle(player, stop, nullptr);
                accepted |= stopped;
                SKSE::log::info("Native idle stop accepted={}", stopped);
            }
            for (const auto event : {"IdleStop", "OffsetStop", "IdleForceDefaultState"}) {
                accepted |= player->NotifyAnimationGraph(RE::BSFixedString(event));
            }
            return accepted;
        }

        bool OwnsGraph(RE::PlayerCharacter* player)
        {
            return playback && ready.load() && playback->session == session_id.load() && player &&
                player->Is3DLoaded() && playback->root.get() == player->Get3D(false);
        }

        std::string CannotStart(RE::PlayerCharacter* player)
        {
            if (!player || !player->Is3DLoaded() || player->IsDead()) {
                return "Load a living player before playing an emote.";
            }
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                return "Close the pause menu before playing an emote.";
            }
            const auto* state = player->AsActorState();
            if (!state) {
                return "Player animation state is unavailable.";
            }
            if (state->IsSwimming()) {
                return "Leave the water before playing an emote.";
            }
            if (player->IsOnMount()) {
                return "Dismount before playing an emote.";
            }
            if (player->IsInMidair()) {
                return "Land before playing an emote.";
            }
            if (player->IsInKillMove() || player->IsInRagdollState() || state->IsStaggered()) {
                return "Wait for the current action to finish before playing an emote.";
            }
            return {};
        }

        // Run menu requests on SKSE's main thread so the render thread never accesses actors.
        // The session check prevents a request from running after another character/save loads.
        template <class Action>
        void QueueTask(Action action)
        {
            const auto queued_session_id = session_id.load();
            if (!ready.load()) {
                PublishCommandResult("Load a game before using playback.", true);
                return;
            }
            const auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                PublishCommandResult("SKSE task interface is unavailable.", true);
                return;
            }
            tasks->AddTask([queued_session_id, action = std::move(action)] {
                if (queued_session_id == session_id.load() && ready.load()) {
                    action();
                } else {
                    SKSE::log::debug("Playback task discarded: queuedSession={} currentSession={} ready={}", queued_session_id, session_id.load(), ready.load());
                }
            });
        }

        // A sink is a listener Skyrim calls for graph events. Register it before a prepared
        // start. Keep the object alive for the process because graph sources retain its pointer.
        class CompletionSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event,
                RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
            {
                // Native one-shots report IdleStop. Graph callbacks only enqueue notices
                // because they may run off the main thread or during NotifyAnimationGraph.
                // 0x14 is Skyrim's persistent player reference. Ignore other actors.
                const auto request = active_id.load();
                if (request && event && event->holder && event->holder->GetFormID() == 0x14 && event->tag == "IdleStop") {
                    std::lock_guard lock(completion_mutex);
                    completions.push_back({request, Clock::now()});
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } completion_sink;

        bool SubscribeCompletion(RE::PlayerCharacter* player)
        {
            RE::BSAnimationGraphManagerPtr manager;
            player->GetAnimationGraphManager(manager);
            if (!manager || manager->graphs.empty()) {
                return false;
            }
            // CommonLib returns false for an already registered sink, not just for failure.
            // It also assumes the graph list is nonempty, so check that before subscribing.
            player->AddAnimationGraphEventSink(&completion_sink);
            return true;
        }

        HeldState CurrentHeldState(RE::PlayerCharacter* player)
        {
            if (!OwnsGraph(player) || playback->phase == Phase::WaitingForWeapon || playback->phase == Phase::Preparing) {
                return {};
            }
            HeldState current{playback->options.state_scope, playback->state, {}};
            if (playback->phase == Phase::Sequence && playback->step_started) {
                current.entering = EnteringState{playback->steps[playback->step_index], playback->elapsed,
                    playback->step_index + 1 == playback->steps.size()};
            }
            return current;
        }

        void RejectAction(RE::PlayerCharacter* player, const std::string& event)
        {
            // A failed page-turn/rise must not throw away a reading/hovering state already reached.
            // Keep the parent pose, but stop retrying the rejected action or repeating its event.
            if (!playback->state.empty()) {
                playback->phase = Phase::Playing;
                playback->options.mode = PlaybackMode::Loop;
                playback->options.state = playback->state;
                playback->options.duration_seconds = playback->options.repeat_interval_seconds = 0.0;
                playback->elapsed = 0.0;
                PublishStatus();
                PublishCommandResult("Action rejected: " + event + ". Current state kept.", true);
            } else {
                const bool native_idle = playback->native_idle_started;
                ForgetPlayback();
                ResetGraph(player, native_idle);
                PublishCommandResult("Animation rejected: " + event, true);
            }
        }

        bool SendMainEvent(RE::PlayerCharacter* player)
        {
            const auto& reference = playback->options.idle_form;
            if (!reference) {
                const bool accepted = player->NotifyAnimationGraph(RE::BSFixedString(playback->event));
                SKSE::log::debug("Graph event: request={} event={} accepted={}", playback->request, playback->event, accepted);
                return accepted;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* idle = data ? data->LookupForm<RE::TESIdleForm>(reference->id, reference->plugin) : nullptr;
            auto* process = player->GetActorRuntimeData().currentProcess;
            // The IDLE record selects engine action context. Sending only its ENAM string
            // can fail even when that event and its clip both exist in the behavior graph.
            if (!idle || !process || idle->animEventName != RE::BSFixedString(playback->event)) {
                SKSE::log::warn("Native idle unavailable or event mismatch: {}|{:08X} event={}",
                    reference->plugin, reference->id, playback->event);
                return false;
            }
            const bool accepted = process->PlayIdle(player, idle, nullptr);
            playback->native_idle_started |= accepted;
            SKSE::log::info("Native idle start: {}|{:08X} event={} accepted={}",
                reference->plugin, reference->id, playback->event, accepted);
            return accepted;
        }

        void TryStep(RE::PlayerCharacter* player)
        {
            const auto& step = playback->steps[playback->step_index];
            const bool main = playback->step_index + 1 == playback->steps.size();
            if (!SubscribeCompletion(player) || !(main ? SendMainEvent(player) :
                player->NotifyAnimationGraph(RE::BSFixedString(step.event)))) {
                const bool stateful = !playback->options.state.empty() || !playback->options.requires_state.empty() ||
                    !playback->options.setup.empty();
                if (!stateful) {
                    const auto event = step.event;
                    RejectAction(player, event);
                } else {
                    // Native state transitions can accept the follow-up shortly after its timer.
                    // Retry rejected stateful events every 250 ms for at most four seconds.
                    if (playback->elapsed == 0.0) {
                        PublishCommandResult("Waiting for the animation state...");
                    }
                    playback->retry_wait = 0.0;
                }
                return;
            }
            SKSE::log::debug("Playback step accepted: request={} step={} event={} main={} waitSeconds={} elapsedBeforeAcceptance={}",
                playback->request, playback->step_index, step.event, main, step.seconds, playback->elapsed);
            playback->step_started = true;
            playback->elapsed = playback->retry_wait = 0.0;
            playback->previous_tick = Clock::now();
            if (playback->step_index + 1 == playback->steps.size()) {
                playback->started = playback->previous_tick;
                if (step.seconds == 0.0) {
                    playback->state = playback->options.state;
                    playback->phase = Phase::Playing;
                }
                PublishCommandResult((playback->options.mode == PlaybackMode::Loop ? "Looping " : "Started ") + playback->name + ".");
            } else {
                PublishCommandResult("Entering " + (step.state_after.empty() ? playback->name : step.state_after) + "...");
            }
            PublishStatus();
        }

        void PrepareAfterSheathing(RE::PlayerCharacter* player)
        {
            const auto weapon = player->AsActorState()->GetWeaponState();
            if (weapon == RE::WEAPON_STATE::kSheathed) {
                ResetGraph(player);
                playback->phase = Phase::Preparing;
                playback->elapsed = 0.0;
                PublishCommandResult("Preparing " + playback->name + "...");
                PublishStatus();
            } else if (weapon == RE::WEAPON_STATE::kDrawn && !playback->sheath_requested) {
                player->DrawWeaponMagicHands(false);
                playback->sheath_requested = true;
            }
            // WantToDraw/Drawing must finish before requesting sheathing. WantToSheathe/
            // Sheathing already belong to Skyrim, so neither command nor reset is sent.
        }

        void TickPlayback()
        {
            if (!playback) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!OwnsGraph(player)) {
                // Never send old cleanup events into a newly loaded save or rebuilt model.
                ForgetPlayback();
                return;
            }
            const auto now = Clock::now();
            // Bound timer advancement after a stalled frame instead of skipping whole animation phases.
            const auto delta = std::clamp(std::chrono::duration<double>(now - playback->previous_tick).count(), 0.0, 0.25);
            playback->previous_tick = now;
            // tfc 1 freezes the actor without necessarily opening a paused menu.
            // Keep the current phase and refresh previous_tick so resuming cannot
            // expire an emote or skip its entry while the engine is frozen.
            auto* ui = RE::UI::GetSingleton();
            const auto* main = RE::Main::GetSingleton();
            const auto* camera = RE::PlayerCamera::GetSingleton();
            const bool free_camera = camera && camera->IsInFreeCameraMode();
            const bool paused = (ui && ui->GameIsPaused()) || (main && main->GetRuntimeData().freezeTime);
            if (playback->free_camera != free_camera || playback->clock_paused != paused) {
                SKSE::log::debug("Playback camera/clock: request={} freeCamera={} paused={}",
                    playback->request, free_camera, paused);
                playback->free_camera = free_camera;
                playback->clock_paused = paused;
            }
            if (paused) return;
            const auto* state = player->AsActorState();
            if (player->IsDead() || player->IsInKillMove() || player->IsInRagdollState() ||
                (state && state->IsStaggered())) {
                // The game owns these transitions. Do not force its graph back to standing.
                ForgetPlayback();
                PublishCommandResult("Emote interrupted.");
                return;
            }
            // Free-camera controls move the camera. They must not cancel the actor's idle.
            // The active graph's Speed can miss first-person movement. Gameplay input
            // also catches held keys, a controller stick and auto-move in either view.
            // Authored walking emotes may move themselves, but user movement still cancels.
            if (const auto* controls = RE::PlayerControls::GetSingleton(); controls && !free_camera) {
                const auto& movement = controls->data.moveInputVec;
                // Ignore small residual stick input. Auto-move is an explicit movement request.
                if (controls->data.autoMove || movement.x * movement.x + movement.y * movement.y > 0.01F) {
                    InterruptPlayback();
                    return;
                }
            }
            float speed = 0.0F;
            player->GetGraphVariableFloat("Speed", speed);
            // Ignore small residual speeds while an idle settles. Authored walking gestures
            // may move themselves, but actual movement buttons still interrupt through Input.
            // Do not cancel on weapon state here. Native idles can draw a prop/weapon during
            // their own entry. Ready Weapon and attack inputs already interrupt through Input.
            if (!state || state->IsSwimming() || player->IsOnMount() || player->IsInMidair() ||
                (!free_camera && !playback->options.allow_movement && speed > 5.0F)) {
                InterruptPlayback();
                return;
            }
            playback->elapsed += delta;

            bool finished = false;
            {
                std::lock_guard lock(completion_mutex);
                for (const auto& completion : completions) {
                    // Ignore reset/entry notifications. Otherwise the previous emote's Stop
                    // can finish its replacement before that replacement has started.
                    finished |= playback->phase == Phase::Playing && completion.request == playback->request &&
                        completion.when >= playback->started + std::chrono::milliseconds(100);
                }
                completions.clear();
            }
            if (playback->phase == Phase::WaitingForWeapon) {
                if (playback->elapsed >= 4.0) {
                    ForgetPlayback();
                    PublishCommandResult("Weapon transition did not finish. Emote cancelled.", true);
                } else PrepareAfterSheathing(player);
            } else if (playback->phase == Phase::Preparing) {
                // Keep Stop/Start separation after the weapon has fully sheathed.
                if (playback->elapsed >= 4.0) {
                    ForgetPlayback();
                    PublishCommandResult("The player did not become ready for the emote.", true);
                } else if (playback->elapsed >= 0.6 && state->GetWeaponState() == RE::WEAPON_STATE::kSheathed &&
                           SubscribeCompletion(player)) {
                    playback->phase = Phase::Sequence;
                    playback->elapsed = 0.0;
                    TryStep(player);
                }
            } else if (playback->phase == Phase::Sequence) {
                if (!playback->step_started) {
                    playback->retry_wait += delta;
                    if (playback->elapsed >= 4.0) {
                        const auto event = playback->steps[playback->step_index].event;
                        RejectAction(player, event);
                    } else if (playback->retry_wait >= 0.25) {
                        TryStep(player);
                    }
                } else if (playback->elapsed >= playback->steps[playback->step_index].seconds) {
                    const auto& step = playback->steps[playback->step_index];
                    if (!step.state_after.empty()) {
                        playback->state = step.state_after;
                    }
                    playback->elapsed = 0.0;
                    if (playback->step_index + 1 == playback->steps.size()) {
                        playback->phase = Phase::Playing;
                        PublishStatus();
                    } else {
                        ++playback->step_index;
                        playback->step_started = false;
                        TryStep(player);
                    }
                }
            } else if (finished && playback->options.repeat_interval_seconds == 0.0 && playback->options.state.empty()) {
                // Native completion already returned the actor to normal. Another reset causes a seam.
                // Named states survive their gesture's IdleStop, such as returning to reading after a page turn.
                // Anonymous graph loops end here. Timed repeats expect IdleStop at each cycle.
                const auto name = playback->name;
                ForgetPlayback();
                PublishCommandResult("Finished " + name + ".");
            } else if (playback->options.duration_seconds > 0.0 &&
                       playback->elapsed >= playback->options.duration_seconds) {
                const auto name = playback->name;
                const bool native_idle = playback->native_idle_started;
                ForgetPlayback();
                ResetGraph(player, native_idle);
                PublishCommandResult("Finished " + name + ".");
            } else if (playback->options.repeat_interval_seconds > 0.0 &&
                       playback->elapsed >= playback->options.repeat_interval_seconds) {
                // Repeat only authored native one-shots. True graph loops receive one start.
                // Send at most once per update so a pause/stall cannot produce a catch-up burst.
                playback->elapsed = 0.0;
                if (!SendMainEvent(player)) {
                    const auto event = playback->event;
                    RejectAction(player, event);
                }
            }
        }
    }

    void QueuePlay(std::string event, std::string name, PlaybackOptions options)
    {
        const auto request = ++request_id;
        SKSE::log::info("Playback request: id={} session={} event={} name={} mode={} state={} setupSteps={} entrySeconds={} durationSeconds={} repeatSeconds={}",
            request, session_id.load(), event, name, options.mode == PlaybackMode::Loop ? "loop" : "once",
            options.state, options.setup.size(), options.entry_seconds, options.duration_seconds, options.repeat_interval_seconds);
        // Nonfinite/negative clocks cannot finish reliably. Validate timing, not catalogue membership.
        if (!std::isfinite(options.duration_seconds) || !std::isfinite(options.repeat_interval_seconds) ||
            !std::isfinite(options.entry_seconds) || options.entry_seconds < 0.0 ||
            options.duration_seconds < 0.0 || options.repeat_interval_seconds < 0.0 ||
            (options.mode == PlaybackMode::Once && options.repeat_interval_seconds != 0.0) ||
            (options.mode == PlaybackMode::Loop && options.duration_seconds != 0.0) ||
            (!options.state.empty() && options.mode != PlaybackMode::Loop) ||
            ((options.entry_seconds > 0.0 || options.state_only) && options.state.empty()) ||
            !std::all_of(options.setup.begin(), options.setup.end(), [](const PlaybackStep& step) {
                return std::isfinite(step.seconds) && step.seconds > 0.0;
            })) {
            PublishCommandResult("Invalid playback timing.", true);
            return;
        }
        QueueTask([event, name, options, request] {
            if (request != request_id.load()) {
                SKSE::log::debug("Playback request replaced: id={} current={}", request, request_id.load());
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (auto reason = CannotStart(player); !reason.empty()) {
                PublishCommandResult(std::move(reason), true);
                return;
            }
            const auto plan = PlanStateSequence(event, options, CurrentHeldState(player));
            SKSE::log::debug("Playback plan: request={} reuse={} preserveFirst={} noOp={} steps={} initialState={}",
                request, plan.reuse, plan.preserve_first, plan.no_op, plan.steps.size(), plan.initial_state);
            if (plan.no_op) {
                PublishCommandResult(options.state.empty() ? "That state is not active." : "Keeping the current animation.");
                return;
            }
            const bool native_idle = plan.reuse && OwnsGraph(player) && playback->native_idle_started;
            // Native idle forms need their special-idle exit before an unrelated replacement.
            // Release them while Drawn too. ResetGraph preserves weapon transitions.
            if (!plan.reuse && OwnsGraph(player) && playback->phase != Phase::WaitingForWeapon &&
                (playback->native_idle_started || player->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kDrawn)) {
                ResetGraph(player, playback->native_idle_started);
            }
            // Publish one replacement snapshot, without briefly showing an empty state in between.
            ForgetPlayback(false);
            playback.emplace(Playback{event, name.empty() ? event : name, options,
                session_id.load(), request, RE::NiPointer<RE::NiAVObject>(player->Get3D(false))});
            playback->native_idle_started = native_idle;
            playback->steps = plan.steps;
            playback->state = plan.initial_state;
            active_id.store(request);
            if (plan.reuse) {
                // Preserve the accepted entry's clock and graph state. Never reset or resend it.
                playback->phase = Phase::Sequence;
                playback->step_started = plan.preserve_first;
                playback->elapsed = plan.first_elapsed;
                if (!plan.preserve_first) {
                    TryStep(player);
                } else {
                    PublishStatus();
                    PublishCommandResult("Continuing entry for " + playback->name + "...");
                }
            } else {
                PublishStatus();
                if (player->AsActorState()->GetWeaponState() != RE::WEAPON_STATE::kSheathed) {
                    PublishCommandResult("Waiting for weapon sheathing...");
                }
                PrepareAfterSheathing(player);
            }
        });
    }

    void QueueStop()
    {
        const auto request = ++request_id;
        SKSE::log::info("Playback stop requested: id={} session={}", request, session_id.load());
        QueueTask([request] {
            if (request != request_id.load()) {
                SKSE::log::debug("Playback request replaced: id={} current={}", request, request_id.load());
                return;
            }
            const bool waiting = playback && playback->phase == Phase::WaitingForWeapon;
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool native_idle = OwnsGraph(player) && playback->native_idle_started;
            ForgetPlayback();
            if (!player || !player->Is3DLoaded() || player->IsDead()) {
                PublishCommandResult("Player animation graph is unavailable.", true);
                return;
            }
            const auto weapon = player->AsActorState()->GetWeaponState();
            if (waiting || (weapon != RE::WEAPON_STATE::kSheathed && weapon != RE::WEAPON_STATE::kDrawn)) {
                PublishCommandResult(waiting ? "Pending emote cancelled. Weapon transition left to Skyrim." :
                    "Weapon transition in progress. No animation reset sent.");
                return;
            }
            const bool accepted = ResetGraph(player, native_idle);
            PublishCommandResult(accepted ? "Stop events sent." : "Animation graph rejected stop events.", !accepted);
        });
    }

    void InterruptPlayback()
    {
        ++request_id;
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* state = player ? player->AsActorState() : nullptr;
        const bool reset = OwnsGraph(player) && playback->phase != Phase::WaitingForWeapon && !player->IsDead() && !player->IsInKillMove() &&
            !player->IsInRagdollState() && state && !state->IsStaggered();
        const bool had_playback = playback.has_value();
        const bool native_idle = reset && playback->native_idle_started;
        ForgetPlayback();
        if (reset) {
            ResetGraph(player, native_idle);
        }
        if (had_playback) {
            PublishCommandResult("Emote interrupted.");
        }
    }

    void QueuePlaybackUpdate()
    {
        if (!active_id.load() || update_pending.exchange(true)) {
            return;
        }
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            update_pending.store(false);
            return;
        }
        tasks->AddTask([] {
            TickPlayback();
            update_pending.store(false);
        });
    }

    void SetPlaybackReady(bool value)
    {
        SKSE::log::info("Playback readiness: ready={} nextSession={}", value, session_id.load() + 1);
        ready.store(false);
        ++session_id;
        playback_status.store(std::make_shared<const PlaybackStatus>());
        PublishCommandResult({});
        ready.store(value);
        QueuePlaybackUpdate();
    }

    std::shared_ptr<const PlaybackStatus> GetPlaybackStatus()
    {
        return playback_status.load();
    }

    std::shared_ptr<const CommandResult> GetCommandResult()
    {
        return billboard.load();
    }
}
