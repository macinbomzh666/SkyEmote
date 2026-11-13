// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>
#include <filesystem>
#include <string>

namespace SkyEmote
{
    inline void ApplyMenuStyle(const std::filesystem::path& font)
    {
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 0.0F;
        style.ChildRounding = 0.0F;
        style.FrameRounding = 0.0F;
        style.PopupRounding = 0.0F;
        style.TabRounding = 0.0F;
        style.ScrollbarRounding = 0.0F;
        style.GrabRounding = 0.0F;
        style.WindowBorderSize = 1.0F;
        style.FrameBorderSize = 0.0F;
        style.PopupBorderSize = 1.0F;
        style.TabBorderSize = 0.0F;
        style.WindowPadding = ImVec2(24.0F, 20.0F);
        style.FramePadding = ImVec2(12.0F, 5.0F);
        style.ItemSpacing = ImVec2(10.0F, 8.0F);
        style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
        style.ScrollbarSize = 12.0F;
        style.WindowTitleAlign = ImVec2(0.5F, 0.5F);

        auto& colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.92F, 0.93F, 0.91F, 1.0F);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.54F, 0.54F, 0.51F, 1.0F);
        colors[ImGuiCol_WindowBg] = ImVec4(0.03F, 0.035F, 0.04F, 0.96F);
        colors[ImGuiCol_ChildBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
        colors[ImGuiCol_PopupBg] = ImVec4(0.055F, 0.058F, 0.061F, 0.98F);
        colors[ImGuiCol_Border] = ImVec4(0.49F, 0.48F, 0.44F, 0.8F);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
        colors[ImGuiCol_TitleBg] = colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.065F, 0.068F, 0.071F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.09F, 0.095F, 0.10F, 0.8F);
        colors[ImGuiCol_Button] = ImVec4(0.07F, 0.075F, 0.08F, 0.5F);
        colors[ImGuiCol_FrameBgHovered] = colors[ImGuiCol_ButtonHovered] = ImVec4(0.24F, 0.25F, 0.26F, 1.0F);
        colors[ImGuiCol_FrameBgActive] = colors[ImGuiCol_ButtonActive] = ImVec4(0.33F, 0.34F, 0.35F, 1.0F);
        colors[ImGuiCol_Tab] = colors[ImGuiCol_TabDimmed] = ImVec4(0.07F, 0.075F, 0.08F, 1.0F);
        colors[ImGuiCol_TabHovered] = ImVec4(0.27F, 0.28F, 0.29F, 1.0F);
        colors[ImGuiCol_TabSelected] = colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.19F, 0.20F, 0.21F, 1.0F);
        colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.78F, 0.80F, 0.79F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.16F, 0.17F, 0.18F, 1.0F);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.25F, 0.26F, 0.27F, 1.0F);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.32F, 0.33F, 0.34F, 1.0F);
        colors[ImGuiCol_Separator] = ImVec4(0.48F, 0.50F, 0.50F, 0.7F);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.15F);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.32F, 0.33F, 0.34F, 1.0F);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.48F, 0.49F, 0.50F, 1.0F);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.64F, 0.65F, 0.66F, 1.0F);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.50F, 0.49F, 0.45F, 0.2F);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.68F, 0.67F, 0.61F, 0.6F);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.83F, 0.81F, 0.74F, 1.0F);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.36F, 0.37F, 0.38F, 0.8F);
        colors[ImGuiCol_NavCursor] = ImVec4(0.83F, 0.81F, 0.74F, 1.0F);

        // ImGui owns this font until its context is destroyed. Recreating the renderer
        // loads it again. Missing fonts fall back without affecting playback availability.
        auto& io = ImGui::GetIO();
        std::error_code error;
        if (std::filesystem::is_regular_file(font, error)) {
            const auto utf8 = font.u8string();
            const std::string filename(utf8.begin(), utf8.end());
            io.FontDefault = io.Fonts->AddFontFromFileTTF(filename.c_str(), 20.0F);
        }
        if (!io.FontDefault) {
            ImFontConfig fallback;
            fallback.SizePixels = 20.0F;
            io.FontDefault = io.Fonts->AddFontDefaultVector(&fallback);
        }
        style.FontSizeBase = 20.0F;
    }
}
