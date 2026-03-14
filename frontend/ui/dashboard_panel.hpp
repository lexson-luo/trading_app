#pragma once
#include <vector>
#include <nlohmann/json.hpp>
#include "../app_state.hpp"
#include "../api_client.hpp"

namespace hf::ui {

class DashboardPanel {
public:
    void render(AppState& state, client::ApiClient& api);
private:
    float refresh_timer{0.0f};
    static constexpr float REFRESH_INTERVAL = 5.0f;

    std::vector<nlohmann::json> positions_;
    std::vector<nlohmann::json> sessions_;
    std::vector<nlohmann::json> trades_;
    nlohmann::json              pnl_{};
    bool                        loaded_{false};

    void refresh(AppState& state, client::ApiClient& api);
};

} // namespace hf::ui
