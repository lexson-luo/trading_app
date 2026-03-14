#include "backtest_panel.hpp"
#include "imgui.h"
#include "implot.h"
#include <numeric>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace hf::ui {

static const char* INSTRUMENTS[] = {
    "sgx_wmp", "sgx_smp", "eex_smp", "cme_nfdm", "cme_class_iii"
};
static const char* STRATEGY_TYPES[] = {"mean_reversion", "momentum"};
static const char* STRATEGY_LABELS[] = {"Mean Reversion", "Momentum"};

// ── Helpers ───────────────────────────────────────────────────────────────────
static void metric_box(const char* label, const char* value,
                       ImVec4 color = ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                       float w = 140.0f)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
    ImGui::BeginChild(label, ImVec2(w, 60), true);
    ImGui::TextDisabled("%s", label);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Text("%s", value);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ── Internal methods ──────────────────────────────────────────────────────────
void BacktestPanel::load_results(AppState& state, client::ApiClient& api) {
    auto r = api.get("/api/backtest/results");
    if (r.ok && r.body.contains("data") && r.body["data"].is_array()) {
        results_list_ = r.body["data"].get<std::vector<nlohmann::json>>();
        results_loaded_ = true;
    } else {
        state.set_error("Failed to load backtest results.");
    }
}

void BacktestPanel::load_result_detail(int64_t id, AppState& state, client::ApiClient& api) {
    auto r = api.get("/api/backtest/results/" + std::to_string(id));
    if (r.ok && r.body.contains("data")) {
        current_result_ = r.body["data"];

        // Build equity curve for chart
        equity_x_.clear(); equity_y_.clear(); pnl_hist_.clear();
        if (current_result_.contains("equity_curve") &&
            current_result_["equity_curve"].is_array())
        {
            auto& eq = current_result_["equity_curve"];
            equity_x_.resize(eq.size());
            equity_y_.resize(eq.size());
            std::iota(equity_x_.begin(), equity_x_.end(), 0.0f);
            for (size_t i = 0; i < eq.size(); ++i)
                equity_y_[i] = (float)eq[i].get<double>();
        }
        // Daily P&L as histogram (diff of equity)
        pnl_hist_.resize(equity_y_.size(), 0.0f);
        for (size_t i = 1; i < equity_y_.size(); ++i)
            pnl_hist_[i] = equity_y_[i] - equity_y_[i-1];

        state.set_status("Loaded backtest result #" + std::to_string(id));
    }
}

void BacktestPanel::run_backtest(AppState& state, client::ApiClient& api) {
    if (running_) return;
    running_   = true;
    run_error_.clear();

    nlohmann::json req = {
        {"instrument",     INSTRUMENTS[sel_instrument]},
        {"strategy_type",  STRATEGY_TYPES[sel_strategy]},
        {"start_date",     start_date},
        {"end_date",       end_date},
        {"cutoff",         cutoff},
        {"strategy_name",  strategy_name}
    };

    state.set_status("Running backtest...");
    auto r = api.post("/api/backtest/run", req);
    if (r.ok && r.body.contains("data")) {
        load_results(state, api);
        if (!results_list_.empty()) {
            int64_t new_id = results_list_[0].value("id", (int64_t)0);
            selected_result_idx_ = 0;
            load_result_detail(new_id, state, api);
        }
        state.set_status("Backtest completed.");
    } else {
        run_error_ = r.body.value("error", r.error.empty() ? "Backtest failed." : r.error);
        state.set_error(run_error_);
    }
    running_ = false;
}

// ── Sub-renderers ─────────────────────────────────────────────────────────────
void BacktestPanel::render_config(AppState& state, client::ApiClient& api) {
    ImGui::Text("Strategy Configuration");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Instrument");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##instr", &sel_instrument, INSTRUMENTS, 5);
    ImGui::Spacing();

    ImGui::Text("Strategy Type");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##stype", &sel_strategy, STRATEGY_LABELS, 2);
    ImGui::Spacing();

    ImGui::Text("Start Date");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##start", start_date, sizeof(start_date));
    ImGui::Spacing();

    ImGui::Text("End Date");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##end", end_date, sizeof(end_date));
    ImGui::Spacing();

    ImGui::Text("Train / Test Split");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##cutoff", &cutoff, 0.50f, 0.90f, "%.2f");
    ImGui::Spacing();

    ImGui::Text("Save As");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", strategy_name, sizeof(strategy_name));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (running_) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.50f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.25f, 1.0f));
    if (ImGui::Button(running_ ? "Running..." : "Run Backtest", ImVec2(-1, 38)))
        run_backtest(state, api);
    ImGui::PopStyleColor(2);
    if (running_) ImGui::EndDisabled();

    if (!run_error_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextWrapped("%s", run_error_.c_str());
        ImGui::PopStyleColor();
    }
}

void BacktestPanel::render_results_list(AppState& state, client::ApiClient& api) {
    ImGui::Text("Past Results");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##results"))
        load_results(state, api);
    ImGui::Separator();

    ImGui::BeginChild("##results_list", ImVec2(-1, 0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (int i = 0; i < (int)results_list_.size(); ++i) {
        auto& res = results_list_[i];
        char lbl[128];
        std::snprintf(lbl, sizeof(lbl), "#%lld  %s  [%s]",
            (long long)res.value("id",(int64_t)0),
            res.value("instrument","?").c_str(),
            res.value("run_at","?").substr(0,10).c_str());

        bool sel = (selected_result_idx_ == i);
        if (ImGui::Selectable(lbl, sel)) {
            selected_result_idx_ = i;
            load_result_detail(res.value("id",(int64_t)0), state, api);
        }
    }
    ImGui::EndChild();
}

void BacktestPanel::render_metrics() {
    if (current_result_.empty()) return;
    auto& m = current_result_.contains("metrics") ? current_result_["metrics"]
                                                   : current_result_;
    double total   = m.value("total_pnl",     0.0);
    double sharpe  = m.value("sharpe_ratio",   0.0);
    double mdd     = m.value("max_drawdown",   0.0);
    double rr      = m.value("risk_reward_ratio", 0.0);
    double var1    = m.value("var_1pct",       0.0);
    double var5    = m.value("var_5pct",       0.0);
    double wr      = m.value("win_rate",       0.0) * 100.0;

    ImVec4 pnl_col = total >= 0 ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.9f,0.25f,0.25f,1);
    ImVec4 shp_col = sharpe >= 1 ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.95f,0.85f,0.35f,1);

    char tbuf[32];
    std::snprintf(tbuf, sizeof(tbuf), "$%.2f", total);
    metric_box("Total P&L",     tbuf, pnl_col);   ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "%.3f", sharpe);
    metric_box("Sharpe Ratio",  tbuf, shp_col);   ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "$%.2f", mdd);
    metric_box("Max Drawdown",  tbuf, ImVec4(0.9f,0.3f,0.3f,1)); ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "%.2f", rr);
    metric_box("Risk/Reward",   tbuf); ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "$%.2f", var1);
    metric_box("VaR 1%",        tbuf, ImVec4(0.9f,0.5f,0.2f,1)); ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "$%.2f", var5);
    metric_box("VaR 5%",        tbuf, ImVec4(0.9f,0.6f,0.3f,1)); ImGui::SameLine(0,8);

    std::snprintf(tbuf, sizeof(tbuf), "%.1f%%", wr);
    ImVec4 wr_col = wr >= 50 ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.9f,0.25f,0.25f,1);
    metric_box("Win Rate",      tbuf, wr_col);
}

void BacktestPanel::render_charts() {
    if (equity_y_.empty()) return;

    if (ImPlot::BeginPlot("Portfolio Equity Curve", ImVec2(-1, 220))) {
        ImPlot::SetupAxes("Day (Out-of-Sample)", "Cumulative P&L (USD)");
        ImPlot::SetupAxisFormat(ImAxis_Y1, "$%.0f");
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.26f, 0.65f, 0.98f, 1.0f));
        ImPlot::PlotLine("Portfolio", equity_x_.data(), equity_y_.data(), (int)equity_y_.size());
        ImPlot::PopStyleColor();
        ImPlot::EndPlot();
    }

    // Daily P&L histogram
    if (!pnl_hist_.empty()) {
        ImGui::Spacing();
        if (ImPlot::BeginPlot("Daily P&L Distribution", ImVec2(-1, 180))) {
            ImPlot::SetupAxes("P&L (USD)", "Frequency");
            ImPlot::SetupAxisFormat(ImAxis_X1, "$%.0f");
            double min_v = *std::min_element(pnl_hist_.begin(), pnl_hist_.end());
            double max_v = *std::max_element(pnl_hist_.begin(), pnl_hist_.end());
            ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.26f, 0.65f, 0.98f, 0.5f));
            ImPlot::PlotHistogram("Daily P&L", pnl_hist_.data(), (int)pnl_hist_.size(),
                                  30, 1.0, ImPlotRange(min_v, max_v));
            ImPlot::PopStyleColor();
            ImPlot::EndPlot();
        }
    }
}

void BacktestPanel::render_spread_table() {
    if (!current_result_.contains("spread_results") ||
        !current_result_["spread_results"].is_array()) return;
    auto& spreads = current_result_["spread_results"];
    if (spreads.empty()) return;

    ImGui::Spacing();
    ImGui::Text("Per-Spread Results");
    ImGui::Spacing();
    if (ImGui::BeginTable("##spreads", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
        ImVec2(0, 180)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Spread",          ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Stationary",      ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("ADF p-val",       ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("In-Sample P&L",   ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Out-Sample P&L",  ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Window",          ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (auto& sp : spreads) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sp.value("name","?").c_str());
            ImGui::TableSetColumnIndex(1);
            bool stat = sp.value("is_stationary", false);
            ImGui::PushStyleColor(ImGuiCol_Text,
                stat ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.9f,0.5f,0.2f,1));
            ImGui::TextUnformatted(stat ? "Yes" : "No");
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", sp.value("adf_pvalue", 1.0));
            ImGui::TableSetColumnIndex(3);
            double isp = sp.value("in_sample_pnl", 0.0);
            ImGui::PushStyleColor(ImGuiCol_Text,
                isp >= 0 ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.9f,0.25f,0.25f,1));
            ImGui::Text("$%.2f", isp);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(4);
            double osp = sp.value("out_sample_pnl", 0.0);
            ImGui::PushStyleColor(ImGuiCol_Text,
                osp >= 0 ? ImVec4(0.15f,0.85f,0.35f,1) : ImVec4(0.9f,0.25f,0.25f,1));
            ImGui::Text("$%.2f", osp);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(5);
            if (sp.contains("best_params") && !sp["best_params"].is_null())
                ImGui::Text("w=%d  n=%.2f", sp["best_params"].value("rolling_window",0),
                            sp["best_params"].value("n_stdv",0.0));
            else
                ImGui::TextDisabled("--");
        }
        ImGui::EndTable();
    }
}

// ── Main render ───────────────────────────────────────────────────────────────
void BacktestPanel::render(AppState& state, client::ApiClient& api) {
    if (!results_loaded_) load_results(state, api);

    ImGui::BeginChild("##bt_scroll", ImVec2(0, 0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::Text("Backtesting");
    ImGui::Separator();
    ImGui::Spacing();

    // Two-column layout: config left, results right
    float left_w = 280.0f;
    ImGui::BeginChild("##bt_left", ImVec2(left_w, 0), true);
    render_config(state, api);
    ImGui::Spacing();
    render_results_list(state, api);
    ImGui::EndChild();

    ImGui::SameLine(0, 8);

    ImGui::BeginChild("##bt_right", ImVec2(0, 0), false);
    if (current_result_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Run a backtest or select a past result.");
    } else {
        // Result header
        char hdr[128];
        std::snprintf(hdr, sizeof(hdr), "Result #%lld  -  %s  [%s  %s]",
            (long long)current_result_.value("id",(int64_t)0),
            current_result_.value("instrument","?").c_str(),
            current_result_.value("start_date","?").c_str(),
            current_result_.value("end_date","?").c_str());
        ImGui::Text("%s", hdr);
        ImGui::Separator();
        ImGui::Spacing();
        render_metrics();
        ImGui::Spacing();
        render_charts();
        render_spread_table();
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

} // namespace hf::ui
