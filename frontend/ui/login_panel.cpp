#include "login_panel.hpp"
#include "imgui.h"
#include <cstring>

namespace hf::ui {

void LoginPanel::do_login(AppState& state, client::ApiClient& api) {
    if (logging_in) return;
    if (std::strlen(username_buf) == 0 || std::strlen(password_buf) == 0) {
        last_error = "Username and password are required.";
        return;
    }
    logging_in = true;
    last_error.clear();

    auto resp = api.post("/api/auth/login", {
        {"username", username_buf},
        {"password", password_buf}
    });

    if (resp.ok && resp.body.contains("access_token")) {
        std::string token = resp.body["access_token"].get<std::string>();
        api.set_token(token);
        state.access_token  = token;
        state.username      = resp.body.value("username", std::string(username_buf));
        state.role          = resp.body.value("role", "trader");
        state.logged_in     = true;
        state.current_screen = AppScreen::DASHBOARD;
        state.set_status("Welcome, " + state.username + "!");
        // Clear sensitive buffers
        std::memset(password_buf, 0, sizeof(password_buf));
    } else {
        if (resp.body.contains("error"))
            last_error = resp.body["error"].get<std::string>();
        else if (!resp.error.empty())
            last_error = resp.error;
        else
            last_error = "Login failed (HTTP " + std::to_string(resp.status) + ")";
    }
    logging_in = false;
}

void LoginPanel::render(AppState& state, client::ApiClient& api) {
    ImGuiIO& io = ImGui::GetIO();
    float win_w = 420.0f, win_h = 340.0f;
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - win_w) * 0.5f, (io.DisplaySize.y - win_h) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Always);

    ImGui::Begin("##login", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    // Title
    ImGui::Spacing();
    float text_w = ImGui::CalcTextSize("Hedge Fund Trading System").x;
    ImGui::SetCursorPosX((win_w - text_w) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.65f, 0.98f, 1.0f));
    ImGui::Text("Hedge Fund Trading System");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float label_w = 110.0f;
    float field_w = win_w - label_w - 32.0f;

    // Username
    ImGui::Text("Username");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    bool enter_pressed = ImGui::InputText("##username", username_buf, sizeof(username_buf),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    // Password
    ImGui::Text("Password");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    ImGuiInputTextFlags pw_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (!show_password) pw_flags |= ImGuiInputTextFlags_Password;
    enter_pressed |= ImGui::InputText("##password", password_buf, sizeof(password_buf), pw_flags);
    ImGui::Spacing();

    ImGui::SetCursorPosX(label_w);
    ImGui::Checkbox("Show password", &show_password);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Login button
    if (logging_in) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(win_w - 32.0f);
    ImGui::SetCursorPosX(16.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.50f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.28f, 1.0f));
    bool clicked = ImGui::Button(logging_in ? "Logging in..." : "Login",
                                 ImVec2(win_w - 32.0f, 36.0f));
    ImGui::PopStyleColor(2);
    if (logging_in) ImGui::EndDisabled();

    if ((clicked || enter_pressed) && !logging_in)
        do_login(state, api);

    // Error
    if (!last_error.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", last_error.c_str());
        ImGui::PopStyleColor();
    }

    // Footer
    float footer_y = win_h - 28.0f;
    ImGui::SetCursorPosY(footer_y);
    ImGui::Separator();
    ImGui::SetCursorPosX(8.0f);
    ImGui::TextDisabled("Backend: %s:%d", api.host().c_str(), api.port());

    ImGui::End();
}

} // namespace hf::ui
