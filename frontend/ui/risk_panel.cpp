#include "risk_panel.hpp"
#include "imgui.h"
#include "implot.h"
#include <cstdio>
#include <algorithm>

namespace hf::ui {

void RiskPanel::load_results(client::ApiClient& api) {
    auto r = api.get("/api/backtest/results");
    if (r.ok && r.body.contains("data") && r.body["data"].is_array()) {
        results_list_ = r.body["data"].get<std::vector<nlohmann::json>>();
        results_loaded_ = true;
    }
}

void RiskPanel::load_risk(AppState& state, client::ApiClient& api, int64_t bt_id) {
    auto r = api.get("/api/risk/var/" + std::to_string(bt_id));
    if (!r.ok) {
        state.set_error(r.body.value("error", "Failed to load risk data."));
        return;
    }
    risk_data_ = r.body.contains("data") ? r.body["data"] : r.body;

    var1_  = (float)risk_data_.value("var_1pct",  0.0);
    var5_  = (float)risk_data_.value("var_5pct",  0.0);
    cvar1_ = (float)risk_data_.value("cvar_1pct", 0.0);
    cvar5_ = (float)risk_data_.value("cvar_5pct", 0.0);

    // Rebuild daily P&L histogram from equity_curve if present
    pnl_hist_.clear();
    auto bt_r = api.get("/api/backtest/results/" + std::to_string(bt_id));
    if (bt_r.ok && bt_r.body.contains("data")) {
        auto& bd = bt_r.body["data"];
        if (bd.contains("equity_curve") && bd["equity_curve"].is_array()) {
            auto& eq = bd["equity_curve"];
            pnl_hist_.reserve(eq.size());
            float prev = 0.0f;
            for (auto& v : eq) {
                float cur = (float)v.get<double>();
                pnl_hist_.push_back(cur - prev);
                prev = cur;
            }
        }
    }
    state.set_status("Risk metrics loaded for backtest #" + std::to_string(bt_id));
}

// ── Sub-renderers ─────────────────────────────────────────────────────────────
void RiskPanel::render_metric_row() {
    auto risk_box = [](const char* lbl, double val, const char* fmt,
                       ImVec4 col = ImVec4(0.90f,0.40f,0.20f,1.0f)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f,0.14f,0.18f,1));
        ImGui::BeginChild(lbl, ImVec2(148, 62), true);
        ImGui::TextDisabled("%s", lbl);
        ImGui::Spacing();
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, val);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%s", buf);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    };

    double sharpe = risk_data_.value("sharpe_ratio",    0.0);
    double mdd    = risk_data_.value("max_drawdown",    0.0);
    double rr     = risk_data_.value("risk_reward_ratio",0.0);
    double wr     = risk_data_.value("win_rate",        0.0) * 100.0;

    ImVec4 red    = ImVec4(0.90f,0.25f,0.25f,1.0f);
    ImVec4 orange = ImVec4(0.90f,0.50f,0.20f,1.0f);
    ImVec4 blue   = ImVec4(0.26f,0.65f,0.98f,1.0f);
    ImVec4 green  = ImVec4(0.15f,0.85f,0.35f,1.0f);

    risk_box("VaR 1%  (1-day)",  var1_,  "$%.2f", red);     ImGui::SameLine(0,8);
    risk_box("VaR 5%  (1-day)",  var5_,  "$%.2f", orange);  ImGui::SameLine(0,8);
    risk_box("CVaR 1%",          cvar1_, "$%.2f", red);      ImGui::SameLine(0,8);
    risk_box("CVaR 5%",          cvar5_, "$%.2f", orange);   ImGui::SameLine(0,8);
    risk_box("Sharpe Ratio",     sharpe, "%.3f",
             sharpe >= 1 ? green : (sharpe >= 0 ? blue : red));  ImGui::SameLine(0,8);
    risk_box("Max Drawdown",     mdd,    "$%.2f", red);      ImGui::SameLine(0,8);
    risk_box("Risk/Reward",      rr,     "%.2f",
             rr >= 1 ? green : orange);                          ImGui::SameLine(0,8);
    risk_box("Win Rate",         wr,     "%.1f%%",
             wr >= 50 ? green : red);
}

void RiskPanel::render_stress_section() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Stress Test  (4x Volatility Scenario)");
    ImGui::Spacing();

    double svar    = risk_data_.value("stressed_var_1pct",    0.0);
    int    breaches= risk_data_.value("stressed_breach_count", 0);
    double normal  = (double)var1_;

    char buf[64];
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f,0.10f,0.10f,1));
    ImGui::BeginChild("##stress_box", ImVec2(0, 90), true);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.45f,0.20f,1.0f));
    ImGui::Spacing();
    std::snprintf(buf, sizeof(buf), "  Stressed VaR 1%%: $%.2f", svar);
    ImGui::Text("%s", buf);
    std::snprintf(buf, sizeof(buf), "  Normal VaR 1%%:   $%.2f", normal);
    ImGui::Text("%s", buf);
    std::snprintf(buf, sizeof(buf), "  Multiplier:       %.1fx", (normal != 0 ? svar/normal : 4.0));
    ImGui::Text("%s", buf);
    ImGui::PopStyleColor();
    ImGui::Text("  VaR Breaches under stress scenario: %d days", breaches);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void RiskPanel::render_pnl_chart() {
    if (pnl_hist_.size() < 5) return;
    ImGui::Spacing();
    if (ImPlot::BeginPlot("Daily P&L Distribution", ImVec2(-1, 240))) {
        ImPlot::SetupAxes("P&L (USD)", "Frequency");
        ImPlot::SetupAxisFormat(ImAxis_X1, "$%.0f");

        float min_v = *std::min_element(pnl_hist_.begin(), pnl_hist_.end());
        float max_v = *std::max_element(pnl_hist_.begin(), pnl_hist_.end());

        ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.26f,0.65f,0.98f,0.45f));
        ImPlot::PlotHistogram("Daily P&L", pnl_hist_.data(), (int)pnl_hist_.size(),
                              40, 1.0, ImPlotRange((double)min_v, (double)max_v));
        ImPlot::PopStyleColor();

        // VaR vertical lines
        if (var1_ != 0) {
            double xv1[2] = {var1_, var1_};
            double yv1[2] = {0, 9999};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.9f,0.25f,0.25f,1));
            ImPlot::PlotLine("VaR 1%", xv1, yv1, 2);
            ImPlot::PopStyleColor();
        }
        if (var5_ != 0) {
            double xv5[2] = {var5_, var5_};
            double yv5[2] = {0, 9999};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.9f,0.55f,0.2f,1));
            ImPlot::PlotLine("VaR 5%", xv5, yv5, 2);
            ImPlot::PopStyleColor();
        }
        ImPlot::EndPlot();
    }
}

// ── Main render ───────────────────────────────────────────────────────────────
void RiskPanel::render(AppState& state, client::ApiClient& api) {
    if (!results_loaded_) load_results(api);

    ImGui::BeginChild("##risk_scroll", ImVec2(0,0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::Text("Risk Analytics");
    ImGui::Separator();
    ImGui::Spacing();

    if (results_list_.empty()) {
        ImGui::TextDisabled("No backtest results found. Run a backtest first.");
        ImGui::EndChild();
        return;
    }

    // Selector
    ImGui::Text("Select Backtest Result:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(350.0f);
    const char* preview = results_list_[sel_result_idx_].value("instrument","?").c_str();
    if (ImGui::BeginCombo("##risk_result", preview)) {
        for (int i = 0; i < (int)results_list_.size(); ++i) {
            char lbl[128];
            std::snprintf(lbl, sizeof(lbl), "#%lld  %s  [%s]",
                (long long)results_list_[i].value("id",(int64_t)0),
                results_list_[i].value("instrument","?").c_str(),
                results_list_[i].value("run_at","?").substr(0,10).c_str());
            bool sel = (sel_result_idx_ == i);
            if (ImGui::Selectable(lbl, sel)) {
                sel_result_idx_ = i;
                int64_t id = results_list_[i].value("id",(int64_t)0);
                load_risk(state, api, id);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Load")) {
        if (!results_list_.empty()) {
            int64_t id = results_list_[sel_result_idx_].value("id",(int64_t)0);
            load_risk(state, api, id);
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (risk_data_.empty()) {
        ImGui::TextDisabled("Select a backtest result above to load risk metrics.");
    } else {
        render_metric_row();
        render_stress_section();
        render_pnl_chart();
    }

    ImGui::EndChild();
}

} // namespace hf::ui
