#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <nlohmann/json.hpp>
#include "../common/types.hpp"

namespace hf {

enum class AppScreen {
    LOGIN,
    DASHBOARD,
    BACKTESTING,
    LIVE_TRADING,
    RISK_ANALYTICS
};

struct AppState {
    // ── Auth ──────────────────────────────────────────────────────────────────
    std::atomic<bool> logged_in{false};
    std::string       username;
    std::string       role;
    std::string       access_token;

    // ── Navigation ────────────────────────────────────────────────────────────
    AppScreen current_screen{AppScreen::LOGIN};

    // ── Status / error ────────────────────────────────────────────────────────
    char status_bar[256]{};
    bool show_error_modal{false};
    char error_message[512]{};

    void set_status(const std::string& msg) {
        std::snprintf(status_bar, sizeof(status_bar), "%s", msg.c_str());
    }
    void set_error(const std::string& msg) {
        std::snprintf(error_message, sizeof(error_message), "%s", msg.c_str());
        show_error_modal = true;
    }
    void logout() {
        logged_in     = false;
        access_token  = "";
        username      = "";
        role          = "";
        current_screen = AppScreen::LOGIN;
        set_status("Logged out.");
    }
};

} // namespace hf
