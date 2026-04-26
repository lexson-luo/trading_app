// GLEW MUST be the very first OpenGL header included.
// Never include SDL3/SDL_opengl.h or GL/gl.h before this line —
// they pull in glext.h which conflicts with GLEW's own declarations.
#include <GL/glew.h>
// SDL3 context creation (do NOT also include SDL3/SDL_opengl.h here)
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "api_client.hpp"
#include "app_state.hpp"
#include "ui/login_panel.hpp"
#include "ui/sidebar.hpp"
#include "ui/dashboard_panel.hpp"
#include "ui/backtest_panel.hpp"
#include "ui/trading_panel.hpp"
#include "ui/risk_panel.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string host = "100.80.205.52";
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        if (a == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
    }

    // ── SDL3 init ─────────────────────────────────────────────────────────────
    // Force X11 backend so GLEW (GLX-based) can initialize correctly.
    // Wayland sessions use EGL which is incompatible with GLEW.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "Hedge Fund Trading System",
        1400, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // vsync

    // ── GLEW init ─────────────────────────────────────────────────────────────
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        std::cerr << "glewInit failed: " << glewGetErrorString(glew_err) << "\n";
        return 1;
    }

    // ── ImGui + ImPlot init ───────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = "hf_client_layout.ini";

    // Dark hedge-fund theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style       = ImGui::GetStyle();
    style.WindowRounding    = 5.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.ItemSpacing       = ImVec2(8, 6);
    style.WindowPadding     = ImVec2(12, 10);
    auto* c = style.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.22f, 0.42f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.18f, 0.38f, 0.68f, 0.80f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.26f, 0.52f, 0.90f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.14f, 0.30f, 0.60f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.18f, 0.38f, 0.68f, 0.45f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.26f, 0.52f, 0.90f, 0.80f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_Tab]            = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    c[ImGuiCol_TabHovered]     = ImVec4(0.26f, 0.52f, 0.90f, 0.80f);
    c[ImGuiCol_TabSelected]    = ImVec4(0.18f, 0.38f, 0.68f, 1.00f);

    ImGui_ImplSDL3_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ── App objects ───────────────────────────────────────────────────────────
    hf::client::ApiClient  api(host, port);
    hf::AppState           state;
    hf::ui::LoginPanel     login_panel;
    hf::ui::SideBar        sidebar;
    hf::ui::DashboardPanel dashboard;
    hf::ui::BacktestPanel  backtest;
    hf::ui::TradingPanel   trading;
    hf::ui::RiskPanel      risk;

    // ── Main loop ─────────────────────────────────────────────────────────────
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (!state.logged_in) {
            login_panel.render(state, api);
        } else {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
            ImGui::Begin("##main", nullptr,
                ImGuiWindowFlags_NoTitleBar     | ImGuiWindowFlags_NoCollapse  |
                ImGuiWindowFlags_NoResize       | ImGuiWindowFlags_NoMove      |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
            ImGui::PopStyleVar(2);

            // Sidebar
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("##sidebar", ImVec2(200, -28), false);
            ImGui::PopStyleVar();
            sidebar.render(state);
            ImGui::EndChild();

            ImGui::SameLine(0, 0);

            // Content
            ImGui::BeginChild("##content", ImVec2(0, -28), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            switch (state.current_screen) {
                case hf::AppScreen::DASHBOARD:      dashboard.render(state, api); break;
                case hf::AppScreen::BACKTESTING:    backtest.render(state, api);  break;
                case hf::AppScreen::LIVE_TRADING:   trading.render(state, api);   break;
                case hf::AppScreen::RISK_ANALYTICS: risk.render(state, api);      break;
                default: break;
            }
            ImGui::EndChild();

            // Status bar
            float bar_y = vp->WorkPos.y + vp->WorkSize.y - 24;
            ImGui::SetCursorScreenPos(ImVec2(vp->WorkPos.x, bar_y));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
            ImGui::BeginChild("##statusbar", ImVec2(vp->WorkSize.x, 24), false);
            ImGui::SetCursorPosY(4);
            ImGui::TextDisabled("  %s", state.status_bar);
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::End();

            // Error modal
            if (state.show_error_modal) {
                ImGui::OpenPopup("Error##modal");
                state.show_error_modal = false;
            }
            if (ImGui::BeginPopupModal("Error##modal", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                ImGui::TextUnformatted(state.error_message);
                ImGui::PopStyleColor();
                ImGui::Spacing();
                if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        ImGui::Render();
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
