// SPDX-License-Identifier: MIT
#include "PackWarnings.h"
#include "Pack.h"
#include <RE/B/BSAnimationGraphManager.h>
#include <RE/B/BShkbAnimationGraph.h>
#include <RE/H/hkbBehaviorGraph.h>
#include <RE/H/hkbBehaviorGraphData.h>
#include <RE/P/PlayerCharacter.h>
#include <SKSE/SKSE.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>

namespace SkyEmote
{
    namespace
    {
        using Warnings = std::vector<PackWarning>;
        // Startup checks files once. Loaded graph checks append to this immutable baseline.
        std::shared_ptr<const Warnings> file_warnings = std::make_shared<const Warnings>();
        bool has_graph_requirements = false;
        // Render reads a snapshot while SKSE tasks inspect the player's loaded graph.
        std::atomic<std::shared_ptr<const Warnings>> warnings{file_warnings};
        std::atomic<bool> ready{false};
        std::atomic<bool> update_pending{false};
        // Load notifications change this ID so an older probe cannot publish into a new save.
        std::atomic<std::uint64_t> session_id{0};

        struct Revision
        {
            bool inspected = false;
            std::optional<std::int32_t> value;
        };

        Revision InspectRevision(RE::PlayerCharacter* player, const std::string& variable)
        {
            RE::BSAnimationGraphManagerPtr manager;
            if (!player || !player->Is3DLoaded() || !player->GetAnimationGraphManager(manager) || !manager) return {};
            RE::BSSpinLockGuard lock{manager->GetRuntimeData().updateLock};
            // Graph zero is the third-person template even while the first-person graph is active.
            if (manager->graphs.empty() || !manager->graphs[0]) return {};
            const auto* behavior = manager->graphs[0]->behaviorGraph;
            const auto* data = behavior ? behavior->data.get() : nullptr;
            const auto* strings = data ? data->stringData.get() : nullptr;
            if (!strings || !data->variableInitialValues) return {};

            // CommonLib hides Havok 2010's word array. Its header follows hkReferencedObject
            // in SE/AE. Copy that header rather than treating the object as a different C++ type.
            struct Words
            {
                const RE::hkbVariableValue* data;
                std::int32_t size;
                std::uint32_t capacity;
            } words{};
            static_assert(sizeof(RE::hkReferencedObject) == 0x10);
            static_assert(sizeof(Words) == sizeof(RE::hkArray<RE::hkbVariableValue>));
            static_assert(sizeof(RE::hkbVariableValueSet) == 0x40);
            std::memcpy(&words, reinterpret_cast<const std::byte*>(data->variableInitialValues) +
                sizeof(RE::hkReferencedObject), sizeof(words));
            const auto count = strings->variableNames.size();
            if (count < 0 || words.size < count || data->variableInfos.size() < count || (count && !words.data)) return {};
            for (std::int32_t i = 0; i < count; ++i) {
                // Mask the managed-string flag at pointer width. A 32-bit mask truncates
                // addresses above 4 GiB and breaks reads that otherwise look like missing markers.
                std::uintptr_t address{};
                static_assert(sizeof(strings->variableNames[i]) == sizeof(address));
                std::memcpy(&address, &strings->variableNames[i], sizeof(address));
                const auto* name = reinterpret_cast<const char*>(address & ~std::uintptr_t{1});
                if (name && variable == name) {
                    if (data->variableInfos[i].m_type.get() != RE::hkbVariableInfo::VARIABLE_TYPE_INT32) return {};
                    // Never use the mutable live getter as a readiness gate. Its value can
                    // be stale or zero while the loaded template has the correct revision.
                    return {true, words.data[i].i};
                }
            }
            return {true, {}};
        }
    }

    void LoadPackWarnings(const std::filesystem::path& data)
    {
        Warnings loaded;
        has_graph_requirements = false;
        for (const auto& pack : GetPacks().packs) {
            has_graph_requirements |= pack.graph.has_value();
            for (const auto& warning : CheckPackFiles(pack, data)) {
                loaded.push_back({pack.name, warning});
                SKSE::log::warn("Pack {}: {}", pack.id, warning);
            }
        }
        file_warnings = std::make_shared<const Warnings>(loaded);
        warnings.store(file_warnings);
    }

    void SetPackWarningsReady(bool value)
    {
        ready.store(false);
        ++session_id;
        warnings.store(file_warnings);
        ready.store(value);
    }

    void QueuePackWarningsUpdate()
    {
        if (!has_graph_requirements || !ready.load() || update_pending.exchange(true)) return;
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) { update_pending.store(false); return; }
        const auto session = session_id.load();
        tasks->AddTask([session] {
            update_pending.store(false);
            if (!ready.load() || session != session_id.load()) return;
            // Checks run while browsing, at most once per second. A new save checks immediately.
            static auto next = std::chrono::steady_clock::time_point{};
            static std::uint64_t checked_session = 0;
            const auto now = std::chrono::steady_clock::now();
            if (session == checked_session && now < next) return;
            checked_session = session;
            next = now + std::chrono::seconds(1);
            auto updated = *file_warnings;
            const auto previous = warnings.load();
            auto* player = RE::PlayerCharacter::GetSingleton();
            for (const auto& pack : GetPacks().packs) {
                if (!pack.graph) continue;
                const auto result = InspectRevision(player, pack.graph->variable);
                const auto message = CheckGraphRevision(*pack.graph, result.inspected, result.value);
                if (!message.empty()) {
                    // Log a new diagnostic once, rather than every one-second graph probe.
                    const bool reported = std::any_of(previous->begin(), previous->end(), [&](const auto& item) {
                        return item.pack == pack.name && item.message == message;
                    });
                    if (!reported) SKSE::log::warn("Pack {}: {}", pack.id, message);
                    updated.push_back({pack.name, message});
                }
            }
            if (ready.load() && session == session_id.load()) {
                warnings.store(std::make_shared<const Warnings>(updated));
            }
        });
    }

    std::shared_ptr<const std::vector<PackWarning>> GetPackWarnings()
    {
        return warnings.load();
    }
}
