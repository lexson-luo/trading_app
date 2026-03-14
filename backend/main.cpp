#include <httplib.h>
#include <iostream>
#include <csignal>
#include <string>

#include "config.hpp"
#include "utils.hpp"
#include "database/connection.hpp"
#include "database/repository.hpp"
#include "brokers/factory.hpp"
#include "core/live_trader.hpp"
#include "api/auth.hpp"
#include "api/data_api.hpp"
#include "api/strategy_api.hpp"
#include "api/backtest_api.hpp"
#include "api/trading_api.hpp"
#include "api/portfolio_api.hpp"
#include "api/risk_api.hpp"
#include "../common/sha256.hpp"

static httplib::Server* g_server = nullptr;

void handle_signal(int) {
    std::cout << "\n[SERVER] Shutdown signal received\n";
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    auto cfg = hf::Config::from_env();

    // Parse CLI overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            cfg.server_port = std::stoi(argv[++i]);
        else if (arg == "--db" && i + 1 < argc)
            cfg.db_path = argv[++i];
        else if (arg == "--broker" && i + 1 < argc)
            cfg.broker_mode = argv[++i];
        else if (arg == "--broker-url" && i + 1 < argc)
            cfg.broker_url = argv[++i];
        else if (arg == "--help") {
            std::cout << "Usage: hf_server [--port N] [--db PATH] [--broker mock|rest] [--broker-url URL]\n";
            return 0;
        }
    }

    // ── Database ──────────────────────────────────────────────────────────────
    std::cout << "[INIT] Opening database: " << cfg.db_path << "\n";
    hf::db::init_db(cfg.db_path);
    hf::db::Repository repo(hf::db::get_db());

    // Seed default admin user if table is empty
    if (repo.user_count() == 0) {
        std::string salt = hf::utils::generate_token(8);
        std::string hash = hf::crypto::hash_password("admin123", salt);
        repo.create_user("admin", hash, salt, "admin");
        std::cout << "[INIT] Created default admin user (username=admin, password=admin123)\n";
        std::cout << "[INIT] IMPORTANT: Change this password before production use!\n";
    }

    // ── Broker ───────────────────────────────────────────────────────────────
    std::cout << "[INIT] Broker mode: " << cfg.broker_mode << "\n";
    auto broker = hf::brokers::get_broker(cfg.broker_mode, cfg.broker_url, cfg.broker_api_key);

    // ── Live Trader ───────────────────────────────────────────────────────────
    hf::core::LiveTrader live_trader(repo, *broker,
        /*tick_interval_s=*/60,
        cfg.max_position_size,
        cfg.max_daily_loss);

    // ── HTTP Server ───────────────────────────────────────────────────────────
    httplib::Server svr;
    g_server = &svr;
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    // CORS pre-flight
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Health check (no auth required)
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","service":"hf_backend"})", "application/json");
    });

    // Version endpoint
    svr.Get("/version", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"version":"1.0.0","build":"release"})", "application/json");
    });

    // Register all API route groups
    hf::api::register_auth_routes(svr, repo, cfg.jwt_secret);
    hf::api::register_data_routes(svr, repo, cfg.jwt_secret);
    hf::api::register_strategy_routes(svr, repo, cfg.jwt_secret);
    hf::api::register_backtest_routes(svr, repo, cfg.jwt_secret);
    hf::api::register_trading_routes(svr, repo, live_trader, cfg.jwt_secret);
    hf::api::register_portfolio_routes(svr, repo, cfg.jwt_secret);
    hf::api::register_risk_routes(svr, repo, live_trader, cfg.jwt_secret);

    // 404 handler
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404) {
            res.set_content(
                R"({"success":false,"error":"Endpoint not found: )" + req.path + R"("})",
                "application/json");
        }
    });

    // Exception handler
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                  std::exception_ptr ep) {
        std::string msg = "Internal server error";
        try { if (ep) std::rethrow_exception(ep); }
        catch (const std::exception& e) { msg = e.what(); }
        catch (...) {}
        res.status = 500;
        res.set_content(
            "{\"success\":false,\"error\":\"" + msg + "\"}",
            "application/json");
    });

    std::cout << "[SERVER] Hedge Fund Backend listening on 0.0.0.0:" << cfg.server_port << "\n";
    std::cout << "[SERVER] API base URL: http://localhost:" << cfg.server_port << "/api\n";

    svr.listen("0.0.0.0", cfg.server_port);

    std::cout << "[SERVER] Shutdown complete\n";
    return 0;
}
