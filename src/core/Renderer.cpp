// SPDX-License-Identifier: MIT
#include "Renderer.h"
#include "Menu.h"
#include "MenuStyle.h"
#include "Input.h"
#include "Playback.h"
#include "PackWarnings.h"

#include <RE/R/Renderer.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace SkyEmote
{
    namespace
    {
        // The engine owns the framebuffer and its lifetime. Borrow its current RTV
        // on each renderer-end callback so resize cannot leave us holding an old buffer.
        ImGuiContext* gui{};
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        auto previous_frame = std::chrono::steady_clock::now();
        bool initialization_failed{};
        bool frame_failed{};
        bool installed{};

        struct RestoreContext
        {
            // Other plugins can also use ImGui. Restore their context when our callback ends.
            ImGuiContext* previous = ImGui::GetCurrentContext();
            ~RestoreContext() { ImGui::SetCurrentContext(previous); }
        };

        template <class Shader>
        struct SavedShader
        {
            Microsoft::WRL::ComPtr<Shader> shader;
            std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> instances{};
            UINT count = static_cast<UINT>(instances.size());

            ~SavedShader()
            {
                for (UINT i = 0; i < count; ++i) {
                    instances[i]->Release();
                }
            }
        };

        struct RestorePipeline
        {
            // ImGui's DX11 backend restores most state, but omits render targets and these shaders.
            std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
            std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> unordered_views{};
            SavedShader<ID3D11HullShader> hull;
            SavedShader<ID3D11DomainShader> domain;
            SavedShader<ID3D11ComputeShader> compute;

            RestorePipeline()
            {
                context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.GetAddressOf());
                context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
                    static_cast<UINT>(unordered_views.size()), unordered_views.data());
                context->HSGetShader(hull.shader.GetAddressOf(), hull.instances.data(), &hull.count);
                context->DSGetShader(domain.shader.GetAddressOf(), domain.instances.data(), &domain.count);
                context->CSGetShader(compute.shader.GetAddressOf(), compute.instances.data(), &compute.count);
            }

            ~RestorePipeline()
            {
                UINT target_count = 0;
                for (UINT i = 0; i < targets.size(); ++i) {
                    if (targets[i]) {
                        target_count = i + 1;
                    }
                }
                if (target_count == targets.size()) {
                    context->OMSetRenderTargets(target_count, targets.data(), depth.Get());
                } else {
                    // Pixel UAV slots start after render targets. Preserve append/consume counters.
                    std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> counters;
                    counters.fill(UINT(-1));
                    context->OMSetRenderTargetsAndUnorderedAccessViews(target_count, targets.data(), depth.Get(),
                        target_count, static_cast<UINT>(unordered_views.size()) - target_count,
                        unordered_views.data() + target_count, counters.data() + target_count);
                }
                context->HSSetShader(hull.shader.Get(), hull.instances.data(), hull.count);
                context->DSSetShader(domain.shader.Get(), domain.instances.data(), domain.count);
                context->CSSetShader(compute.shader.Get(), compute.instances.data(), compute.count);
                for (auto* view : targets) {
                    if (view) {
                        view->Release();
                    }
                }
                for (auto* view : unordered_views) {
                    if (view) {
                        view->Release();
                    }
                }
            }
        };

        void DestroyRenderer()
        {
            if (!gui) {
                context.Reset();
                device.Reset();
                return;
            }
            ImGui::SetCurrentContext(gui);
            if (ImGui::GetIO().BackendRendererUserData) {
                ImGui_ImplDX11_Shutdown();
            }
            ImGui::DestroyContext(gui);
            gui = nullptr;
            context.Reset();
            device.Reset();
        }

        bool InitializeRenderer(ID3D11Device* new_device, ID3D11DeviceContext* new_context)
        {
            if (!new_device || !new_context || FAILED(new_device->GetDeviceRemovedReason())) return false;

            IMGUI_CHECKVERSION();
            gui = ImGui::CreateContext();
            ImGui::SetCurrentContext(gui);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            const auto game = std::filesystem::path(REL::Module::get().filePath().data()).parent_path();
            ApplyMenuStyle(game / L"Data/SKSE/Plugins/SkyEmote/fonts/Sovngarde Light.ttf");
            // Check shader/resource creation explicitly instead of letting NewFrame assert.
            if (!ImGui_ImplDX11_Init(new_device, new_context) ||
                !ImGui_ImplDX11_CreateDeviceObjects()) {
                DestroyRenderer();
                return false;
            }
            device = new_device;
            context = new_context;
            SKSE::log::info("Renderer initialized: ImGui={} device={} context={}", IMGUI_VERSION,
                static_cast<void*>(new_device), static_cast<void*>(new_context));
            return true;
        }

        bool DrawFrame(ID3D11RenderTargetView* target)
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            target->GetResource(resource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture))) return false;
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            if (!description.Width || !description.Height) return false;
            auto& io = ImGui::GetIO();
            const ImVec2 extent(static_cast<float>(description.Width), static_cast<float>(description.Height));
            if (io.DisplaySize.x != extent.x || io.DisplaySize.y != extent.y) {
                SKSE::log::info("Renderer framebuffer extent: {}x{} format={}",
                    description.Width, description.Height, static_cast<unsigned>(description.Format));
            }
            io.DisplaySize = extent;
            const auto now = std::chrono::steady_clock::now();
            io.DeltaTime = std::clamp(std::chrono::duration<float>(now - previous_frame).count(), 0.001F, 0.25F);
            previous_frame = now;

            ImGui_ImplDX11_NewFrame();
            FeedMenuInput();
            ImGui::NewFrame();
            DrawMenu();
            SetMenuTextInput(ImGui::GetIO().WantTextInput);
            ImGui::Render();
            RestorePipeline restore;
            context->OMSetRenderTargets(1, &target, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            return true;
        }

        void DrawEngineFrame()
        {
            static bool first = true;
            if (first) SKSE::log::info("Renderer-end callback reached");
            first = false;
            QueuePlaybackUpdate();
            if (IsMenuOpen()) QueuePackWarningsUpdate();
            RestoreContext restore;
            const auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            ID3D11RenderTargetView* target{};
            ID3D11Device* current_device{};
            ID3D11DeviceContext* current_context{};
            if (renderer) {
                const auto& data = renderer->GetRuntimeData();
                current_device = reinterpret_cast<ID3D11Device*>(data.forwarder);
                current_context = reinterpret_cast<ID3D11DeviceContext*>(data.context);
                target = reinterpret_cast<ID3D11RenderTargetView*>(data.renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER].RTV);
                if (!target) target = reinterpret_cast<ID3D11RenderTargetView*>(data.renderWindows[0].renderView);
            }
            if (gui && (device.Get() != current_device || context.Get() != current_context ||
                FAILED(device->GetDeviceRemovedReason()))) {
                SKSE::log::warn("Renderer device/context changed or was lost. Releasing ImGui resources");
                DestroyRenderer();
            }
            if (!gui) {
                const bool initialized = InitializeRenderer(current_device, current_context);
                if (!initialized && !initialization_failed) SKSE::log::error("Renderer initialization unavailable");
                if (initialized && initialization_failed) SKSE::log::info("Renderer initialization recovered");
                initialization_failed = !initialized;
            }
            const bool drawable = gui && target;
            bool frame_ok = drawable;
            static bool menu_drawn = false;
            if (drawable && IsMenuOpen()) {
                ImGui::SetCurrentContext(gui);
                const bool drawn = DrawFrame(target);
                frame_ok = drawn;
                if (drawn && !menu_drawn) SKSE::log::info("Menu draw submitted to engine framebuffer");
                menu_drawn = drawn;
            } else {
                menu_drawn = false;
            }
            if (!frame_ok && !frame_failed) SKSE::log::error("Renderer frame failed: gui={} target={} menuOpen={}",
                gui != nullptr, static_cast<void*>(target), IsMenuOpen());
            if (frame_ok && frame_failed) SKSE::log::info("Renderer frame recovered");
            frame_failed = !frame_ok;
            QueueInputUpdate(frame_ok);
        }

        struct RendererEndHook
        {
            // Draw where Skyrim finishes its frame, using the engine's framebuffer.
            // A Present hook's swap-chain buffer can be replaced by upscaling/composition.
            static void Thunk(std::uint32_t timer)
            {
                original(timer);
                DrawEngineFrame();
            }
            static inline REL::Relocation<decltype(Thunk)> original;
        };
    }

    bool InstallRenderer()
    {
        if (installed) return true;
        // The timer call is at the same offset in the SE and AE renderer-end functions.
        // Refuse an unexpected instruction instead of overwriting an unknown hook.
        REL::Relocation<std::uintptr_t> site{RELOCATION_ID(75461, 77246), 0x9};
        const auto opcode = *reinterpret_cast<const std::uint8_t*>(site.address());
        SKSE::log::info("Renderer-end hook: site={:#x} opcode={:#x}", site.address(), opcode);
        if (opcode != 0xE8) {
            SKSE::log::error("Renderer-end hook refused: expected a relative call");
            return false;
        }
        std::int32_t displacement{};
        std::memcpy(&displacement, reinterpret_cast<const void*>(site.address() + 1), sizeof(displacement));
        // Publish the chained function before another render thread can enter our hook.
        RendererEndHook::original = site.address() + 5 + displacement;
        SKSE::AllocTrampoline(64);
        SKSE::GetTrampoline().write_call<5>(site.address(), RendererEndHook::Thunk);
        installed = true;
        SKSE::log::info("Renderer-end hook installed: previous={:#x} TRP={}", RendererEndHook::original.address(),
            GetModuleHandleW(L"TheosRenderPipeline.dll") != nullptr);
        return true;
    }
}
