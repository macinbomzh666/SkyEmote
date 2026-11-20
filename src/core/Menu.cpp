// SPDX-License-Identifier: MIT
#include "Menu.h"
#include "MenuSelection.h"

#include "Playback.h"
#include "Favorites.h"
#include "PackWarnings.h"
#include "Input.h"
#include <SKSE/SKSE.h>
#include <set>
#include <map>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <utility>
#include <format>

namespace SkyEmote
{
    namespace
    {
        // Explicit navigation can show results without selecting any browsing tab.
        MenuNavigation navigation;
        // Loaded before the menu can open. After startup only the render thread edits favorites.
        Favorites favorites;
        std::set<EmoteKey> favorite_keys;
        // Render-thread edits publish copies. Quick Emote reads them on the input
        // thread without racing favorite edits or touching ImGui's private context.
        using Hotkeys = std::map<unsigned, std::optional<Emote>>;
        std::atomic<std::shared_ptr<const Hotkeys>> quick_hotkeys{std::make_shared<const Hotkeys>()};

        void IndexFavorites()
        {
            if (!favorites.Error().empty()) SKSE::log::error("Favorites: {}", favorites.Error());
            favorite_keys.clear();
            for (const auto& key : favorites.Items()) {
                const auto found = FindEmote(GetPacks(), key);
                favorite_keys.insert(found ? EmoteKey{found.pack->id, found.emote->id} : key);
            }
            quick_hotkeys.store(std::make_shared<const Hotkeys>(favorites.ResolveHotkeys(GetPacks())));
        }

        void ToggleFavorite(const EmoteKey& key)
        {
            // Remove the saved alias as well when its current emote is unfavorited.
            auto saved = key;
            for (const auto& item : favorites.Items()) {
                const auto found = FindEmote(GetPacks(), item);
                if (found && found.pack->id == key.pack && found.emote->id == key.emote) {
                    saved = item;
                    break;
                }
            }
            favorites.Toggle(saved);
            IndexFavorites();
        }

        std::string search;
        std::string folded_search;
        // Catalogue entries load once. Rebuild search rows when the query, variant
        // choice or held pose changes, rather than scanning every pack each frame.
        bool search_dirty = true;
        // Picker choices survive tab switches but never change a variant's saved ID.
        VariantChoices selected_variants;
        // Binding targets are edited on the render thread.
        std::optional<EmoteKey> binding_favorite;
        std::string hotkey_notice;

        std::string DisplayLabel(std::string text)
        {
            // Tabs and separators interpret ## as hidden text. Separate literal hash pairs.
            for (std::size_t at = 0; (at = text.find("##", at)) != std::string::npos; at += 2) {
                text.insert(at + 1, " ");
            }
            return text;
        }

        void DrawItemLabel(const std::string& label)
        {
            // Labels are literal text. They must not become ImGui IDs or hide text after ##.
            auto minimum = ImGui::GetItemRectMin();
            auto maximum = ImGui::GetItemRectMax();
            const auto padding = ImGui::GetStyle().FramePadding;
            minimum.x += padding.x;
            minimum.y += padding.y;
            maximum.x -= padding.x;
            ImGui::GetWindowDrawList()->PushClipRect(minimum, maximum, true);
            ImGui::GetWindowDrawList()->AddText(minimum, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            ImGui::GetWindowDrawList()->PopClipRect();
        }

        bool PlayButton(const std::string& label, float width)
        {
            const bool clicked = ImGui::Button("##play", ImVec2(std::max(width, 1.0F), 0.0F));
            DrawItemLabel(label);
            return clicked;
        }

        void ActivateEmote(const Emote& emote)
        {
            hotkey_notice.clear();
            if (emote.action == EmoteAction::Stop) QueueStop();
            else QueuePlay(emote.event, emote.name, emote.playback);
        }

        float FavoriteHotkeyWidth(const EmoteKey& key)
        {
            const auto code = favorites.Hotkey(key);
            const auto label = code ? MenuKeyLabel(code) : "...";
            return std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0F);
        }

        void DrawFavoriteHotkey(const EmoteKey& key)
        {
            const auto code = favorites.Hotkey(key);
            const auto label = code ? MenuKeyLabel(code) : "...";
            if (ImGui::Button((DisplayLabel(label) + "###favorite-key").c_str(), ImVec2(FavoriteHotkeyWidth(key), 0.0F))) {
                binding_favorite = key;
                hotkey_notice.clear();
                CaptureFavoriteKey(true);
            }
            if (ImGui::IsItemHovered() && !IsCapturingFavoriteKey()) {
                ImGui::SetTooltip("Hotkey: %s (menu or Quick Emote). Click then press Escape to clear. Right-click also clears.", code ? label.c_str() : "None");
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                favorites.BindHotkey(key, 0);
                IndexFavorites();
                binding_favorite.reset();
                hotkey_notice.clear();
                CaptureFavoriteKey(false);
            }
        }

        void DrawEmoteTooltip(const Emote& emote, const std::string& label)
        {
            if (!ImGui::IsItemHovered() || !ImGui::BeginTooltip()) return;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 32.0F);
            ImGui::TextUnformatted(label.c_str());
            ImGui::TextDisabled("%s", emote.action == EmoteAction::Stop ? "Stop animation" :
                emote.playback.mode == PlaybackMode::Loop ? "Looping" : "One shot");
            if (!emote.description.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted(emote.description.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        void DrawRow(const Pack& pack, const Category& category, const MenuRow& row,
            const EmoteKey* saved_key = nullptr, bool show_source = false)
        {
            const auto& primary = *row.primary;
            const auto* chosen = row.chosen;
            const auto& family = row.variants;
            const bool favorite = saved_key != nullptr;
            ImGui::PushID(pack.id.c_str());
            ImGui::PushID(primary.id.c_str());
            // A dropdown always identifies its current variant, including the default.
            const auto variant_label = [&](const Emote& item) {
                if (family.size() < 2 || item.name.ends_with(")")) return item.name;
                if (item.id == primary.id) return item.name + " (Default)";
                for (std::size_t i = 1; i < family.size(); ++i) {
                    if (family[i]->id == item.id) return item.name + " (Variant " + std::to_string(i + 1) + ")";
                }
                return item.name + " (Variant)";
            };
            const EmoteKey family_key{pack.id, primary.id};
            const EmoteKey key{pack.id, chosen->id};
            const bool selected = favorite || favorite_keys.contains(key);
            if (ImGui::Button(selected ? "-" : "+", ImVec2(ImGui::GetFrameHeight(), 0.0F))) {
                if (saved_key) {
                    favorites.Toggle(*saved_key);
                    IndexFavorites();
                } else ToggleFavorite(key);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(selected ? "Remove from favorites" : "Add to favorites");
            ImGui::SameLine();
            const auto indicator_x = ImGui::GetCursorPosX();
            ImGui::TextUnformatted(chosen->playback.mode == PlaybackMode::Loop ? "L" : " ");
            if (ImGui::IsItemHovered() && chosen->playback.mode == PlaybackMode::Loop) ImGui::SetTooltip("Looping");
            ImGui::SameLine();
            ImGui::SetCursorPosX(indicator_x + ImGui::CalcTextSize("L").x + ImGui::GetStyle().ItemSpacing.x);
            const auto picker_width = favorite && saved_key ? FavoriteHotkeyWidth(*saved_key) : ImGui::GetFrameHeight();
            const auto button_width = ImGui::GetContentRegionAvail().x - picker_width - ImGui::GetStyle().ItemSpacing.x;
            const auto label = favorite || show_source ? chosen->name + " (" + pack.name + " -> " + category.name + ")" : variant_label(*chosen);
            if (PlayButton(label, button_width)) ActivateEmote(*chosen);
            DrawEmoteTooltip(*chosen, label);
            ImGui::SameLine();
            if (family.size() > 1) {
                if (ImGui::ArrowButton("variants", ImGuiDir_Down)) ImGui::OpenPopup("Variants");
                if (ImGui::BeginPopup("Variants")) {
                    for (const auto* item : family) {
                        ImGui::PushID(item->id.c_str());
                        const auto variant_name = variant_label(*item);
                        if (PlayButton(variant_name, std::max(250.0F, ImGui::CalcTextSize(variant_name.c_str()).x + 24.0F))) {
                            selected_variants[family_key] = item->id;
                            search_dirty = true;
                            ActivateEmote(*item);
                            ImGui::CloseCurrentPopup();
                        }
                        DrawEmoteTooltip(*item, variant_name);
                        ImGui::PopID();
                    }
                    ImGui::EndPopup();
                }
            } else if (favorite && saved_key) DrawFavoriteHotkey(*saved_key);
            else ImGui::Dummy(ImVec2(picker_width, ImGui::GetFrameHeight()));
            ImGui::PopID();
            ImGui::PopID();
        }

        void DrawFavorites(const PlaybackStatus& status)
        {
            const auto saved = favorites.Items();
            bool visible = false;
            const auto columns = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 48.0F ? 2 : 1;
            if (!ImGui::BeginTable("Favorite columns", columns, ImGuiTableFlags_SizingStretchSame)) return;
            for (const auto& key : saved) {
                const auto found = FindEmote(GetPacks(), key);
                if (found) {
                    const auto row = ResolveMenuRow(*found.pack, *found.section,
                        *found.emote, {}, {}, status, true);
                    if (!row.matches) continue;
                    ImGui::TableNextColumn();
                    ImGui::PushID(key.emote.c_str());
                    DrawRow(*found.pack, *found.category, row, &key);
                    ImGui::PopID();
                } else {
                    ImGui::TableNextColumn();
                    ImGui::PushID(key.pack.c_str());
                    ImGui::PushID(key.emote.c_str());
                    if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0.0F))) ToggleFavorite(key);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from favorites");
                    ImGui::SameLine();
                    ImGui::Dummy(ImVec2(ImGui::CalcTextSize("L").x, ImGui::GetFrameHeight()));
                    ImGui::SameLine();
                    ImGui::BeginDisabled();
                    PlayButton(key.emote + " (" + key.pack + ", unavailable)",
                        ImGui::GetContentRegionAvail().x - FavoriteHotkeyWidth(key) - ImGui::GetStyle().ItemSpacing.x);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    DrawFavoriteHotkey(key);
                    ImGui::PopID();
                    ImGui::PopID();
                }
                visible = true;
            }
            ImGui::EndTable();
            if (!visible) ImGui::TextUnformatted("Use + beside an emote to add a favorite.");
        }

        void DrawGroups(const std::vector<MenuGroup>& groups)
        {
            const auto columns = groups.size() > 1 && ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 48.0F ? 2 : 1;
            std::array<std::vector<std::size_t>, 2> stacks;
            std::array<float, 2> heights{};
            const auto& style = ImGui::GetStyle();
            const auto header = ImGui::GetTextLineHeight() + style.SeparatorTextPadding.y * 2.0F + style.ItemSpacing.y;
            // Keep each section whole and place it in the shorter column. Each column gets
            // one table cell, so a tall neighbor never inserts an empty row below a short section.
            // Manifest order is preserved within each column, including after search filtering.
            for (std::size_t i = 0; i < groups.size(); ++i) {
                const auto column = columns == 2 && heights[1] < heights[0] ? 1 : 0;
                stacks[column].push_back(i);
                heights[column] += header + static_cast<float>(groups[i].rows.size()) * ImGui::GetFrameHeightWithSpacing();
            }
            if (!ImGui::BeginTable("Section columns", columns, ImGuiTableFlags_SizingStretchSame)) return;
            for (int column = 0; column < columns; ++column) {
                ImGui::TableNextColumn();
                for (const auto index : stacks[column]) {
                    const auto& group = groups[index];
                    ImGui::SeparatorText(DisplayLabel(group.section->name).c_str());
                    for (const auto& row : group.rows) DrawRow(*group.pack, *group.category, row);
                }
            }
            ImGui::EndTable();
        }

        void DrawSearch(const PlaybackStatus& status)
        {
            static std::vector<MenuGroup> groups;
            static std::string state, scope;
            if (std::exchange(search_dirty, false) || state != status.state || scope != status.state_scope) {
                groups = SearchMenu(GetPacks(), folded_search, status, selected_variants);
                state = status.state;
                scope = status.state_scope;
            }
            if (groups.empty()) ImGui::TextUnformatted("No matching emotes.");
            else {
                const auto columns = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 48.0F ? 2 : 1;
                if (ImGui::BeginTable("Search columns", columns, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& group : groups) for (const auto& row : group.rows) {
                        ImGui::TableNextColumn();
                        DrawRow(*group.pack, *group.category, row, nullptr, true);
                    }
                    ImGui::EndTable();
                }
            }
        }

        void DrawSettings()
        {
            const auto settings = GetInputSettings();
            ImGui::SeparatorText("Playback");
            auto intensity = static_cast<int>(FacePlaybackIntensity());
            ImGui::SetNextItemWidth(std::min(400.0F, ImGui::GetContentRegionAvail().x));
            if (ImGui::SliderInt("Facial expression intensity", &intensity, 0, MaxFacePlaybackIntensity,
                    "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                SetMenuFacePlaybackIntensity(static_cast<unsigned>(intensity));
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveInputSettings();
            ImGui::Spacing();
            auto sensitivity = settings.mouse_sensitivity;
            ImGui::SeparatorText("Input");
            ImGui::SetNextItemWidth(std::min(400.0F, ImGui::GetContentRegionAvail().x));
            ImGui::BeginDisabled(!settings.sensitivity_limits_available);
            if (ImGui::SliderFloat("Mouse sensitivity", &sensitivity, settings.sensitivity_min, settings.sensitivity_max, "%.2fx",
                ImGuiSliderFlags_AlwaysClamp)) {
                SetMenuMouseSensitivity(sensitivity);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveInputSettings();
            ImGui::EndDisabled();
            ImGui::TextWrapped("1.0 follows Skyrim's menu cursor. Resolution is handled by the game. Changes apply immediately.");
            if (!settings.sensitivity_limits_available) ImGui::TextWrapped("Skyrim's sensitivity limits are unavailable.");
            ImGui::Spacing();
            const auto key = settings.capturing_key ? std::string("Press a key...") : MenuKeyLabel(settings.menu_key);
            if (ImGui::Button((key + "###menu-key").c_str())) CaptureMenuKey(!settings.capturing_key);
            ImGui::SameLine();
            ImGui::TextUnformatted("Open menu key");
            ImGui::TextWrapped(settings.capturing_key ? "Press a keyboard key. Escape restores [." :
                "Click the binding to change it. Escape always closes the menu.");
            ImGui::Spacing();
            const auto stop = settings.capturing_stop_key ? std::string("Press a key...") :
                settings.stop_key ? MenuKeyLabel(settings.stop_key) : "...";
            if (ImGui::Button((stop + "###stop-key").c_str())) CaptureStopKey(!settings.capturing_stop_key);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ClearStopKey();
            ImGui::SameLine();
            ImGui::TextUnformatted("Stop animation key");
            ImGui::TextWrapped(settings.capturing_stop_key ? "Press a keyboard key. Escape clears the binding." :
                "Click then press Escape, or right-click, to unbind.");
            ImGui::TextDisabled("Only works outside the menu and Quick Emote.");
            ImGui::Spacing();
            const auto quick = settings.capturing_quick_emote_key ? std::string("Press a key...") :
                settings.quick_emote_key ? MenuKeyLabel(settings.quick_emote_key) : "...";
            if (ImGui::Button((quick + "###quick-emote-key").c_str())) CaptureQuickEmoteKey(!settings.capturing_quick_emote_key);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ClearQuickEmoteKey();
            ImGui::SameLine();
            ImGui::TextUnformatted("Quick Emote key");
            ImGui::TextWrapped("Use favorite hotkeys without opening the menu. Controls stay blocked until you select a favorite, press Escape, or %g seconds pass.", settings.quick_emote_timeout);
            if (settings.capturing_quick_emote_key) ImGui::TextWrapped("Press a keyboard key. Escape clears the binding.");
            if (settings.binding_conflict) ImGui::TextWrapped("That key is already used by another global binding. Choose another.");
            ImGui::Spacing();
            if (ImGui::Button("Reset input settings")) ResetInputSettings();
            ImGui::TextWrapped("Changes are saved automatically to SkyEmote.ini.");
            if (favorites.FindHotkey(settings.menu_key)) {
                ImGui::TextWrapped("The menu key is also assigned to a favorite. Rebind that favorite to use its shortcut.");
            }
        }

        void DrawBrowseTab(const char* label, std::string_view id, ImGuiTabItemFlags flags = 0)
        {
            const bool selected = navigation.page == id;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                ImGui::PushStyleColor(ImGuiCol_TabDimmed, ImGui::GetStyleColorVec4(ImGuiCol_TabDimmedSelected));
            }
            // Regular tab items always select a page. Tab buttons let the search
            // view leave every tab unselected without touching ImGui internals.
            const bool clicked = ImGui::TabItemButton(label, flags);
            if (selected) ImGui::PopStyleColor(2);
            if (clicked) navigation.SelectPage(id);
        }

        void DrawCatalogue(const PlaybackStatus& status, bool search_edited)
        {
            if (search_edited) navigation.EditSearch(!search.empty());
            if (ImGui::BeginTabBar("Packs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                DrawBrowseTab("Settings###settings", "@settings", ImGuiTabItemFlags_Leading);
                DrawBrowseTab("Favorites###favorites", "@favorites");
                // Vanilla precedes addons, which retain the loader's filename order.
                std::vector<const Pack*> ordered;
                for (const auto& pack : GetPacks().packs) {
                    if (pack.id == "vanilla") ordered.insert(ordered.begin(), &pack);
                    else ordered.push_back(&pack);
                }
                for (const auto* item : ordered) {
                    const auto label = DisplayLabel(item->name) + "###pack-" + item->id;
                    DrawBrowseTab(label.c_str(), item->id);
                }
                ImGui::EndTabBar();
            }
            const bool search_active = navigation.page.empty();
            const bool settings_active = navigation.page == "@settings";
            const bool favorites_active = navigation.page == "@favorites";
            const Pack* selected_pack = nullptr;
            const Category* selected_category = nullptr;
            for (const auto& pack : GetPacks().packs) {
                if (pack.id == navigation.page) selected_pack = &pack;
            }
            if (!settings_active) {
                CaptureMenuKey(false);
                CaptureStopKey(false);
                CaptureQuickEmoteKey(false);
            }
            if (!favorites_active) {
                CaptureFavoriteKey(false);
                binding_favorite.reset();
            }
            if (selected_pack) {
                ImGui::PushID(selected_pack->id.c_str());
                if (ImGui::BeginTabBar("Categories", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    for (const auto& category : selected_pack->categories) {
                        const auto name = DisplayLabel(category.name) + "###" + category.id;
                        if (ImGui::BeginTabItem(name.c_str())) {
                            selected_category = &category;
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                }
                ImGui::PopID();
            }
            // Only the list scrolls. Tabs stay in the parent window, and each page keeps
            // its own scroll position through the child ID.
            ImGui::PushID(search_active ? "@search" : settings_active ? "@settings" : selected_pack ? selected_pack->id.c_str() : "@favorites");
            ImGui::PushID(selected_category ? selected_category->id.c_str() : "");
            const auto footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0F + 1.0F;
            const auto height = std::max(1.0F, ImGui::GetContentRegionAvail().y - footer);
            if (search_active && search_edited) ImGui::SetNextWindowScroll(ImVec2(0.0F, 0.0F));
            if (ImGui::BeginChild("Emotes", ImVec2(0.0F, height))) {
                if (settings_active) DrawSettings();
                else if (GetPacks().packs.empty()) ImGui::TextUnformatted("No valid packs loaded. See SkyEmote.log.");
                if (search_active) DrawSearch(status);
                else if (favorites_active) DrawFavorites(status);
                else if (selected_pack && selected_category) {
                    const auto groups = CollectMenuGroups(*selected_pack, *selected_category, {}, status, selected_variants);
                    if (groups.empty()) ImGui::TextUnformatted("No emotes in this category.");
                    else DrawGroups(groups);
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::PopID();
        }

        std::string DisplayState(const PlaybackStatus& status)
        {
            if (status.state.empty()) return "None";
            // Built-in states already have human-readable names. Addon state tokens
            // stay private to playback. Resolve their display text through the catalogue.
            if (status.state_scope != "vanilla") {
                for (const auto& pack : GetPacks().packs) if (pack.id == status.state_scope) {
                    for (const auto& category : pack.categories) for (const auto& section : category.sections) {
                        for (const auto& emote : section.emotes) {
                            if (emote.playback.state == status.state) return emote.name;
                        }
                    }
                }
            }
            return status.state;
        }

        void DrawFooter()
        {
            const auto result = GetCommandResult();
            const auto warnings = GetPackWarnings();
            const auto settings_error = GetInputSettings().save_error;
            const auto errors = GetPacks().errors.size() + (favorites.Error().empty() ? 0 : 1);
            std::string summary;
            if (!hotkey_notice.empty()) summary += hotkey_notice + " ";
            if (!settings_error.empty()) summary += "Settings not saved. ";
            if (errors) summary += std::to_string(errors) + " loading error(s). ";
            if (!warnings->empty()) summary += std::to_string(warnings->size()) + " pack warning(s). ";
            if (errors || !settings_error.empty() || !warnings->empty() || (result && result->error)) summary += "See SkyEmote.log. ";
            summary += result && !result->message.empty() ? result->message : "Ready.";
            std::replace(summary.begin(), summary.end(), '\n', ' ');
            std::replace(summary.begin(), summary.end(), '\r', ' ');
            const bool error = errors || !settings_error.empty() || !hotkey_notice.empty() || (result && result->error);
            const auto color = error ? ImVec4(1.0F, 0.35F, 0.35F, 1.0F) :
                !warnings->empty() ? ImVec4(1.0F, 0.8F, 0.3F, 1.0F) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const auto text_width = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##feedback", ImVec2(std::max(1.0F, text_width), ImGui::GetFrameHeight()));
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            DrawItemLabel(summary);
            ImGui::PopStyleColor();
        }
    }

    void LoadMenuFavorites(const std::filesystem::path& file, const std::filesystem::path& legacy)
    {
        favorites.Load(file, legacy);
        IndexFavorites();
    }

    bool PlayQuickFavoriteHotkey(unsigned scan_code)
    {
        const auto snapshot = quick_hotkeys.load();
        const auto found = snapshot->find(scan_code);
        if (found == snapshot->end()) return false;
        if (!found->second) {
            SKSE::log::warn("Quick Emote favorite unavailable: scan={}", scan_code);
        } else {
            const auto& emote = *found->second;
            SKSE::log::info("Quick Emote favorite selected: scan={} event={}", scan_code, emote.event);
            if (emote.action == EmoteAction::Stop) QueueStop();
            else QueuePlay(emote.event, emote.name, emote.playback);
        }
        return true;
    }

    bool HandleFavoriteHotkey(unsigned scan_code)
    {
        if (!IsMenuOpen()) return false;
        if (!IsCapturingFavoriteKey()) binding_favorite.reset();
        const auto settings = GetInputSettings();
        if (binding_favorite) {
            if (scan_code == settings.menu_key) {
                hotkey_notice = "That key opens the menu. Choose another key.";
            } else if (scan_code > 0 && scan_code <= 255) {
                favorites.BindHotkey(*binding_favorite, (scan_code == 1 || scan_code == 14) ? 0 : scan_code);
                IndexFavorites();
                CaptureFavoriteKey(false);
                binding_favorite.reset();
                hotkey_notice.clear();
            }
            return true;
        }
        // Typing and rebinding take priority over favorite shortcuts.
        if (ImGui::GetIO().WantTextInput || settings.capturing_key || settings.capturing_stop_key || settings.capturing_quick_emote_key) return false;
        const auto key = favorites.FindHotkey(scan_code);
        if (!key) return false;
        const auto found = FindEmote(GetPacks(), *key);
        if (!found) {
            hotkey_notice = "That favorite's pack or emote is unavailable.";
            return true;
        }
        hotkey_notice.clear();
        ActivateEmote(*found.emote);
        CloseMenu();
        return true;
    }

    void DrawMenu()
    {
        const auto* viewport = ImGui::GetMainViewport();
        const auto maximum = ImVec2(viewport->WorkSize.x * 0.95F, viewport->WorkSize.y * 0.95F);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.85F, viewport->WorkSize.y * 0.8F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(440.0F, maximum.x), std::min(360.0F, maximum.y)), maximum);
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5F, 0.5F));
        static const auto version = SKSE::GetPluginVersion();
        // A stable ImGui ID keeps window/tab state independent of the visible version.
        static const auto title = std::format("SkyEmote v{}.{}.{}###SkyEmote", version.major(), version.minor(), version.patch());
        if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const auto status = GetPlaybackStatus();
            const auto label = "State: " + DisplayState(*status) +
                (status->transitioning ? " (transitioning)" : "");
            const auto stop_width = std::max(100.0F, ImGui::CalcTextSize("Stop animation").x + ImGui::GetStyle().FramePadding.x * 4.0F);
            const auto stop_height = ImGui::GetFrameHeight() * 1.25F;
            const auto state_width = ImGui::GetContentRegionAvail().x - stop_width - ImGui::GetStyle().ItemSpacing.x;
            ImGui::InvisibleButton("##state", ImVec2(std::max(1.0F, state_width), stop_height));
            DrawItemLabel(label);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Stop animation", ImVec2(stop_width, stop_height))) {
                QueueStop();
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth(-1.0F);
            const bool search_edited = ImGui::InputTextWithHint("##search", "Search emotes across all packs", &search);
            if (search_edited) {
                folded_search = FoldMenuSearch(search);
                search_dirty = true;
            }
            DrawCatalogue(*status, search_edited);
            ImGui::Separator();
            DrawFooter();
            if (binding_favorite && IsCapturingFavoriteKey()) {
                ImGui::SetTooltip("Press a key to bind. Escape clears the binding.");
            }
        }
        ImGui::End();
    }
}
