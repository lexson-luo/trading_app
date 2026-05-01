#include "dashboard_panel.hpp"
#include "imgui.h"
#include <cstdio>

namespace hf::ui {

// Helpers
static void pnl_colored_text(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%.2f", v);
    if (v > 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.85f, 0.35f, 1.0f));
    else if (v < 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.25f, 0.25f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}

static void metric_card(const char* label, const char* value, float w = 160.0f) {
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
    ImGui::BeginChild(label, ImVec2(w, 64), true);
    ImGui::TextDisabled("%s", label);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.90f, 0.60f, 1.0f));
    ImGui::Text("%s", value);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

void DashboardPanel::refresh(AppState& state, client::ApiClient& api) {
    auto pos_r = api.get("/api/portfolio/positions");
    if (pos_r.ok && pos_r.body.contains("data") && pos_r.body["data"].is_array())
        positions_ = pos_r.body["data"].get<std::vector<nlohmann::json>>();

    auto pnl_r = api.get("/api/portfolio/pnl");
    if (pnl_r.ok && pnl_r.body.contains("data") && !pnl_r.body["data"].is_null())
        pnl_ = pnl_r.body["data"];

    auto sess_r = api.get("/api/trading/sessions");
    if (sess_r.ok && sess_r.body.contains("data") && sess_r.body["data"].is_array())
        sessions_ = sess_r.body["data"].get<std::vector<nlohmann::json>>();

    auto trade_r = api.get("/api/portfolio/trades");
    if (trade_r.ok && trade_r.body.contains("data") && trade_r.body["data"].is_array())
        trades_ = trade_r.body["data"].get<std::vector<nlohmann::json>>();

    loaded_ = true;
    state.set_status("Dashboard refreshed.");
}

void DashboardPanel::render(AppState& state, client::ApiClient& api) {
    refresh_timer += ImGui::GetIO().DeltaTime;
    if (!loaded_ || refresh_timer >= REFRESH_INTERVAL) {
        refresh(state, api);
        refresh_timer = 0.0f;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::BeginChild("##dashboard_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // ── Header ────────────────────────────────────────────────────────────────
    ImGui::Text("Portfolio Dashboard");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
    if (ImGui::SmallButton("Refresh")) {
        refresh(state, api);
        refresh_timer = 0.0f;
    }
    ImGui::Separator();
    ImGui::Spacing();

    // ── Summary cards ─────────────────────────────────────────────────────────
    double total_realized = pnl_.is_object() ? pnl_.value("realized_pnl", 0.0) : 0.0;
    double total_unrealized = pnl_.is_object() ? pnl_.value("unrealized_pnl", 0.0) : 0.0;
    int    pos_count = (int)positions_.size();
    int    active_sessions = 0;
    for (auto& s : sessions_)
        if (s.value("status", "") == "RUNNING") active_sessions++;

    char card_buf[64];
    std::snprintf(card_buf, sizeof(card_buf), "%d", pos_count);
    metric_card("Open Positions", card_buf);
    ImGui::SameLine(0, 10);
    std::snprintf(card_buf, sizeof(card_buf), "$%.2f", total_realized);
    metric_card("Realized P&L", card_buf);
    ImGui::SameLine(0, 10);
    std::snprintf(card_buf, sizeof(card_buf), "$%.2f", total_unrealized);
    metric_card("Unrealized P&L", card_buf);
    ImGui::SameLine(0, 10);
    std::snprintf(card_buf, sizeof(card_buf), "%d", active_sessions);
    metric_card("Active Sessions", card_buf);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Positions table ───────────────────────────────────────────────────────
    ImGui::Text("Open Positions");
    ImGui::Spacing();
    if (ImGui::BeginTable("##positions", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, 140)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Symbol",        ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Net Qty",        ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("Avg Cost",       ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Realized P&L",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Updated",        ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableHeadersRow();
        for (auto& p : positions_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(p.value("symbol","").c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", p.value("net_qty", 0));
            ImGui::TableSetColumnIndex(2); ImGui::Text("$%.4f", p.value("avg_cost", 0.0));
            ImGui::TableSetColumnIndex(3); pnl_colored_text(p.value("realized_pnl", 0.0));
            ImGui::TableSetColumnIndex(4); ImGui::TextDisabled("%s", p.value("updated_at","").c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Live sessions ─────────────────────────────────────────────────────────
    ImGui::Text("Live Sessions");
    ImGui::Spacing();
    if (ImGui::BeginTable("##sessions", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, 120)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Session ID",  ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("Strategy ID", ImGuiTableColumnFlags_WidthFixed,  90);
        ImGui::TableSetupColumn("Status",      ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("Started",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total P&L",   ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableHeadersRow();
        for (auto& s : sessions_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%lld", (long long)s.value("id",0));
            ImGui::TableSetColumnIndex(1); ImGui::Text("%lld", (long long)s.value("strategy_id",0));
            ImGui::TableSetColumnIndex(2);
            auto status = s.value("status","");
            if (status == "RUNNING")
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f,0.85f,0.35f,1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f,0.60f,0.60f,1.0f));
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("%s", s.value("started_at","").c_str());
            ImGui::TableSetColumnIndex(4); pnl_colored_text(s.value("total_pnl", 0.0));
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Recent trades ─────────────────────────────────────────────────────────
    ImGui::Text("Recent Trades");
    ImGui::Spacing();
    if (ImGui::BeginTable("##trades", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, 160)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Symbol",  ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Side",    ImGuiTableColumnFlags_WidthFixed,  60);
        ImGui::TableSetupColumn("Qty",     ImGuiTableColumnFlags_WidthFixed,  60);
        ImGui::TableSetupColumn("Price",   ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("P&L",     ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int show = std::min((int)trades_.size(), 50);
        for (int i = 0; i < show; ++i) {
            auto& t = trades_[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(t.value("symbol","").c_str());
            ImGui::TableSetColumnIndex(1);
            auto side = t.value("side","");
            if (side == "BUY")
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f,0.85f,0.35f,1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f,0.25f,0.25f,1.0f));
            ImGui::TextUnformatted(side.c_str());
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", t.value("quantity",0));
            ImGui::TableSetColumnIndex(3); ImGui::Text("$%.4f", t.value("price",0.0));
            ImGui::TableSetColumnIndex(4); pnl_colored_text(t.value("pnl",0.0));
            ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s", t.value("timestamp","").c_str());
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace hf::ui
