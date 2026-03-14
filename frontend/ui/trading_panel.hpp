#pragma once
#include <vector>
#include <nlohmann/json.hpp>
#include "../app_state.hpp"
#include "../api_client.hpp"

namespace hf::ui {

class TradingPanel {
public:
    void render(AppState& state, client::ApiClient& api);
private:
    std::vector<nlohmann::json> strategies_;
    std::vector<nlohmann::json> sessions_;
    int  sel_strategy_idx_{0};
    bool starting_{false};
    bool strats_loaded_{false};
    bool sessions_loaded_{false};
    float refresh_timer_{0.0f};
    static constexpr float REFRESH_INTERVAL = 4.0f;
    std::string start_error_;

    void load_strategies(AppState& state, client::ApiClient& api);
    void load_sessions(AppState& state, client::ApiClient& api);
    void start_session(AppState& state, client::ApiClient& api);
    void stop_session(int64_t id, AppState& state, client::ApiClient& api);
};

} // namespace hf::ui
