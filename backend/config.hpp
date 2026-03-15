#pragma once
#include <string>
#include <cstdlib>

namespace hf {

struct Config {
    std::string db_path        = "trading_app.db";
    std::string jwt_secret     = "changeme-hedge-fund-secret-2024";
    int         server_port    = 8080;
    std::string bind_addr      = "0.0.0.0"; // restrict to LAN IP via HF_BIND_ADDR
    std::string broker_mode    = "mock";   // "mock" | "rest"
    std::string broker_url     = "";
    std::string broker_api_key = "";
    // Risk limits
    int    max_position_size = 100;
    double max_daily_loss    = 50000.0;
    // Market data table schema
    std::string market_data_table = "market_data";

    static Config from_env() {
        Config cfg;
        auto getenv_str = [](const char* name, const std::string& def) -> std::string {
            const char* v = std::getenv(name);
            return v ? std::string(v) : def;
        };
        auto getenv_int = [](const char* name, int def) -> int {
            const char* v = std::getenv(name);
            return v ? std::stoi(v) : def;
        };
        auto getenv_dbl = [](const char* name, double def) -> double {
            const char* v = std::getenv(name);
            return v ? std::stod(v) : def;
        };
        cfg.bind_addr       = getenv_str("HF_BIND_ADDR",      cfg.bind_addr);
        cfg.db_path         = getenv_str("HF_DB_PATH",        cfg.db_path);
        cfg.jwt_secret      = getenv_str("HF_JWT_SECRET",     cfg.jwt_secret);
        cfg.server_port     = getenv_int("HF_PORT",           cfg.server_port);
        cfg.broker_mode     = getenv_str("HF_BROKER_MODE",    cfg.broker_mode);
        cfg.broker_url      = getenv_str("HF_BROKER_URL",     cfg.broker_url);
        cfg.broker_api_key  = getenv_str("HF_BROKER_API_KEY", cfg.broker_api_key);
        cfg.max_position_size = getenv_int("HF_MAX_POSITION", cfg.max_position_size);
        cfg.max_daily_loss    = getenv_dbl("HF_MAX_DAILY_LOSS", cfg.max_daily_loss);
        return cfg;
    }
};

// Global config singleton (set once at startup)
inline Config& global_config() {
    static Config cfg;
    return cfg;
}

} // namespace hf
