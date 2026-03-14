#pragma once
#include <string>
#include "../app_state.hpp"
#include "../api_client.hpp"

namespace hf::ui {

class LoginPanel {
public:
    void render(AppState& state, client::ApiClient& api);
private:
    char username_buf[128]{};
    char password_buf[128]{};
    bool show_password{false};
    bool logging_in{false};
    std::string last_error;

    void do_login(AppState& state, client::ApiClient& api);
};

} // namespace hf::ui
