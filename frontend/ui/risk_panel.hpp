#pragma once
#include <vector>
#include <nlohmann/json.hpp>
#include "../app_state.hpp"
#include "../api_client.hpp"

namespace hf::ui {

class RiskPanel {
public:
    void render(AppState& state, client::ApiClient& api);
private:
    std::vector<nlohmann::json> results_list_;
    int    sel_result_idx_{0};
    bool   results_loaded_{false};
    nlohmann::json risk_data_;

    // Chart data
    std::vector<float> pnl_hist_;
    float var1_{0.0f}, var5_{0.0f}, cvar1_{0.0f}, cvar5_{0.0f};

    void load_results(client::ApiClient& api);
    void load_risk(AppState& state, client::ApiClient& api, int64_t bt_id);
    void render_metric_row();
    void render_stress_section();
    void render_pnl_chart();
};

} // namespace hf::ui
