#include "trading_panel.hpp"
#include "imgui.h"
#include <cstdio>

namespace hf::ui {

void TradingPanel::load_strategies(AppState& /*state*/, client::ApiClient& api) {
    auto r = api.get("/api/strategies");
    if (r.ok && r.body.contains("data") && r.body["data"].is_array()) {
        strategies_ = r.body["data"].get<std::vector<nlohmann::json>>();
        strats_loaded_ = true;
    }
}

void TradingPanel::load_sessions(AppState& /*state*/, client::ApiClient& api) {
    auto r = api.get("/api/trading/sessions");
    if (r.ok && r.body.contains("data") && r.body["data"].is_array()) {
        sessions_ = r.body["data"].get<std::vector<nlohmann::json>>();
        sessions_loaded_ = true;
    }
}

void TradingPanel::start_session(AppState& state, client::ApiClient& api) {
    if (starting_ || strategies_.empty()) return;
    starting_    = true;
    start_error_.clear();

    int64_t sid = strategies_[sel_strategy_idx_].value("id", (int64_t)0);
    auto r = api.post("/api/trading/start", {{"strategy_id", sid}});
    if (r.ok) {
        load_sessions(state, api);
        state.set_status("Live session started.");
    } else {
        start_error_ = r.body.value("error", r.error.empty() ? "Failed to start session." : r.error);
        state.set_error(start_error_);
    }
    starting_ = false;
}

void TradingPanel::stop_session(int64_t id, AppState& state, client::ApiClient& api) {
    auto r = api.post("/api/trading/stop/" + std::to_string(id), {});
    if (r.ok) {
        load_sessions(state, api);
        state.set_status("Session #" + std::to_string(id) + " stopped.");
    } else {
        state.set_error(r.body.value("error", "Failed to stop session."));
    }
}

void TradingPanel::render(AppState& state, client::ApiClient& api) {
    if (!strats_loaded_)   load_strategies(state, api);
    if (!sessions_loaded_) load_sessions(state, api);

    refresh_timer_ += ImGui::GetIO().DeltaTime;
    if (refresh_timer_ >= REFRESH_INTERVAL) {
        load_sessions(state, api);
        refresh_timer_ = 0.0f;
    }

    ImGui::BeginChild("##trade_scroll", ImVec2(0, 0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::Text("Live Trading");
    ImGui::Separator();
    ImGui::Spacing();

    // ── Left: start session ────────────────────────────────────────────────
    float left_w = 300.0f;
    ImGui::BeginChild("##trade_left", ImVec2(left_w, 0), true);

    ImGui::Text("Start New Session");
    ImGui::Separator();
    ImGui::Spacing();

    if (strategies_.empty()) {
        ImGui::TextDisabled("No strategies found.");
        ImGui::TextDisabled("Run a backtest first to create one.");
    } else {
        ImGui::Text("Strategy");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##strat_combo",
            strategies_[sel_strategy_idx_].value("name","?").c_str()))
        {
            for (int i = 0; i < (int)strategies_.size(); ++i) {
                bool sel = (sel_strategy_idx_ == i);
                if (ImGui::Selectable(
                    strategies_[i].value("name","?").c_str(), sel))
                    sel_strategy_idx_ = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();

        // Selected strategy info
        auto& s = strategies_[sel_strategy_idx_];
        ImGui::TextDisabled("Instrument: %s", s.value("instrument","?").c_str());
        ImGui::TextDisabled("Type:       %s", s.value("type","?").c_str());
        ImGui::TextDisabled("Created:    %s", s.value("created_at","?").substr(0,10).c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Warning
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.20f, 1.0f));
        ImGui::TextWrapped(
            "WARNING: Live trading routes real orders to the broker. "
            "Ensure risk limits are configured before starting.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (starting_) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.50f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.25f, 1.0f));
        if (ImGui::Button(starting_ ? "Starting..." : "Start Live Trading",
                          ImVec2(-1, 42)))
            start_session(state, api);
        ImGui::PopStyleColor(2);
        if (starting_) ImGui::EndDisabled();

        if (!start_error_.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
            ImGui::TextWrapped("%s", start_error_.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::SameLine(0, 8);

    // ── Right: active sessions ─────────────────────────────────────────────
    ImGui::BeginChild("##trade_right", ImVec2(0, 0), false);

    ImGui::Text("Live Sessions");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton("Refresh##sess")) {
        load_sessions(state, api);
        refresh_timer_ = 0.0f;
    }
    ImGui::Separator();
    ImGui::Spacing();

    if (sessions_.empty()) {
        ImGui::TextDisabled("No sessions found.");
    } else {
        if (ImGui::BeginTable("##sess_table", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID",          ImGuiTableColumnFlags_WidthFixed,  50);
            ImGui::TableSetupColumn("Strategy",    ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableSetupColumn("Status",      ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableSetupColumn("Started",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Total P&L",   ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Action",      ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableHeadersRow();

            for (auto& s : sessions_) {
                ImGui::TableNextRow();
                int64_t sid = s.value("id", (int64_t)0);

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%lld", (long long)sid);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%lld", (long long)s.value("strategy_id",(int64_t)0));

                ImGui::TableSetColumnIndex(2);
                auto status = s.value("status","");
                bool running = (status == "RUNNING");
                if (running)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f,0.85f,0.35f,1));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f,0.60f,0.60f,1));
                ImGui::TextUnformatted(status.c_str());
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", s.value("started_at","").c_str());

                ImGui::TableSetColumnIndex(4);
                double pnl = s.value("total_pnl", 0.0);
                if (pnl > 0)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f,0.85f,0.35f,1));
                else if (pnl < 0)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f,0.25f,0.25f,1));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f,0.7f,0.7f,1));
                ImGui::Text("$%.2f", pnl);
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(5);
                if (running) {
                    char stop_lbl[32];
                    std::snprintf(stop_lbl, sizeof(stop_lbl), "Stop##%lld", (long long)sid);
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f,0.1f,0.1f,1));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f,0.15f,0.15f,1));
                    if (ImGui::SmallButton(stop_lbl))
                        stop_session(sid, state, api);
                    ImGui::PopStyleColor(2);
                } else {
                    ImGui::TextDisabled("--");
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild(); // right
    ImGui::EndChild(); // scroll
}

} // namespace hf::ui
