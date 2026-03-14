#pragma once
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "../app_state.hpp"
#include "../api_client.hpp"

namespace hf::ui {

class BacktestPanel {
public:
    void render(AppState& state, client::ApiClient& api);
private:
    // Config inputs
    int  sel_instrument{0};
    int  sel_strategy{0};
    char start_date[16]{"2019-01-01"};
    char end_date[16]{"2023-11-27"};
    float cutoff{0.70f};
    char strategy_name[128]{"TermSpread_MR"};
    bool running_{false};
    std::string run_error_;

    // Results list
    std::vector<nlohmann::json> results_list_;
    int  selected_result_idx_{-1};
    bool results_loaded_{false};

    // Current result details
    nlohmann::json current_result_;
    std::vector<float> equity_x_, equity_y_;
    std::vector<float> pnl_hist_;

    void run_backtest(AppState& state, client::ApiClient& api);
    void load_results(AppState& state, client::ApiClient& api);
    void load_result_detail(int64_t id, AppState& state, client::ApiClient& api);
    void render_config(AppState& state, client::ApiClient& api);
    void render_results_list(AppState& state, client::ApiClient& api);
    void render_metrics();
    void render_charts();
    void render_spread_table();
};

} // namespace hf::ui
