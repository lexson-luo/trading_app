#include "sidebar.hpp"
#include "imgui.h"

namespace hf::ui {

static void nav_button(const char* label, AppScreen target, AppState& state) {
    bool active = (state.current_screen == target);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.68f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button(label, ImVec2(200.0f, 40.0f)))
        state.current_screen = target;
    ImGui::PopStyleColor();
}

void SideBar::render(AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.10f, 1.0f));
    ImGui::BeginChild("##sidebar_inner", ImVec2(200, 0), false);

    // Logo / title
    ImGui::Spacing();
    ImGui::SetCursorPosX(12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.65f, 0.98f, 1.0f));
    ImGui::Text("HF Trading");
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(12.0f);
    ImGui::TextDisabled("%s  [%s]",
        state.username.c_str(),
        state.role.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Navigation
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.08f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.38f, 0.68f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.38f, 0.68f, 1.0f));

    nav_button("  Dashboard",      AppScreen::DASHBOARD,      state);
    nav_button("  Backtesting",    AppScreen::BACKTESTING,    state);
    nav_button("  Live Trading",   AppScreen::LIVE_TRADING,   state);
    nav_button("  Risk Analytics", AppScreen::RISK_ANALYTICS, state);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // Logout at bottom
    float h = ImGui::GetWindowHeight();
    ImGui::SetCursorPosY(h - 52.0f);
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.12f, 0.12f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("  Logout", ImVec2(200.0f, 36.0f)))
        state.logout();
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace hf::ui
