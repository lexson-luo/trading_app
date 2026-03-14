#include "risk_api.hpp"
#include "auth.hpp"
#include "../core/risk_engine.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <numeric>

namespace hf::api {

static void set_error(httplib::Response& res, const std::string& raw, int def = 500) {
    std::string msg = raw;
    int status = def;
    if (msg.size() >= 4 && msg[3] == ':') {
        try { status = std::stoi(msg.substr(0,3)); msg = msg.substr(4); } catch(...) {}
    }
    res.status = status;
    res.set_content(
        nlohmann::json{{"success",false},{"error",msg}}.dump(),
        "application/json");
}

// Extract daily_pnl from spread_results JSON array
static std::vector<double> extract_portfolio_pnl(const nlohmann::json& spread_results) {
    std::vector<double> portfolio;
    for (auto& sr : spread_results) {
        if (!sr.contains("daily_pnl")) continue;
        auto& dpnl = sr["daily_pnl"];
        if (!dpnl.is_array()) continue;
        if (portfolio.empty()) {
            portfolio.resize(dpnl.size(), 0.0);
        }
        size_t min_len = std::min(portfolio.size(), dpnl.size());
        for (size_t i = 0; i < min_len; ++i) {
            portfolio[i] += dpnl[i].get<double>();
        }
        if (dpnl.size() > portfolio.size()) {
            for (size_t i = portfolio.size(); i < dpnl.size(); ++i)
                portfolio.push_back(dpnl[i].get<double>());
        }
    }
    return portfolio;
}

void register_risk_routes(httplib::Server&  svr,
                           db::Repository&   repo,
                           core::LiveTrader& live_trader,
                           const std::string& jwt_secret) {

    // GET /api/risk/var/{backtest_id}
    svr.Get(R"(/api/risk/var/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id = std::stoll(req.matches[1].str());
            auto row = repo.get_backtest_result(id);
            if (!row) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Backtest result not found"}}.dump(),
                    "application/json");
                return;
            }

            nlohmann::json spreads_j;
            try { spreads_j = nlohmann::json::parse(row->spread_results); }
            catch(...) {
                res.status = 422;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Cannot parse spread results"}}.dump(),
                    "application/json");
                return;
            }

            auto pnl = extract_portfolio_pnl(spreads_j);
            if (pnl.empty()) {
                res.set_content(
                    nlohmann::json{{"success",false},{"error","No PnL data in result"}}.dump(),
                    "application/json");
                return;
            }

            double var_1pct  = core::RiskEngine::compute_var(pnl, 0.99);
            double var_5pct  = core::RiskEngine::compute_var(pnl, 0.95);
            double cvar_1pct = core::RiskEngine::compute_cvar(pnl, var_1pct);
            double cvar_5pct = core::RiskEngine::compute_cvar(pnl, var_5pct);
            double sharpe    = core::RiskEngine::compute_sharpe(pnl);
            auto equity      = core::RiskEngine::build_equity_curve(pnl);
            double max_dd    = core::RiskEngine::compute_max_drawdown(equity);
            double rr        = core::RiskEngine::compute_risk_reward(pnl);

            res.set_content(
                nlohmann::json{
                    {"success",           true},
                    {"backtest_id",       id},
                    {"var_1pct",          var_1pct},
                    {"var_5pct",          var_5pct},
                    {"cvar_1pct",         cvar_1pct},
                    {"cvar_5pct",         cvar_5pct},
                    {"sharpe_ratio",      sharpe},
                    {"max_drawdown",      max_dd},
                    {"risk_reward_ratio", rr},
                    {"n_days",            pnl.size()}
                }.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/risk/stress-test/{backtest_id}
    svr.Get(R"(/api/risk/stress-test/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id = std::stoll(req.matches[1].str());
            double factor = 4.0;
            if (!req.get_param_value("factor").empty())
                factor = std::stod(req.get_param_value("factor"));

            auto row = repo.get_backtest_result(id);
            if (!row) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Backtest result not found"}}.dump(),
                    "application/json");
                return;
            }

            nlohmann::json spreads_j;
            try { spreads_j = nlohmann::json::parse(row->spread_results); }
            catch(...) { spreads_j = nlohmann::json::array(); }

            auto pnl = extract_portfolio_pnl(spreads_j);
            if (pnl.empty()) {
                res.set_content(
                    nlohmann::json{{"success",false},{"error","No PnL data"}}.dump(),
                    "application/json");
                return;
            }

            auto [stressed_var, breach_count] = core::RiskEngine::stress_test(pnl, factor);
            double normal_var = core::RiskEngine::compute_var(pnl, 0.99);

            res.set_content(
                nlohmann::json{
                    {"success",           true},
                    {"backtest_id",       id},
                    {"stress_factor",     factor},
                    {"normal_var_1pct",   normal_var},
                    {"stressed_var_1pct", stressed_var},
                    {"breach_count",      breach_count},
                    {"breach_rate",       pnl.size() > 0 ?
                        static_cast<double>(breach_count) / pnl.size() : 0.0}
                }.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/risk/live/{session_id}
    svr.Get(R"(/api/risk/live/(\d+))", [&repo, &live_trader, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t session_id = std::stoll(req.matches[1].str());

            auto sess = live_trader.get_session(session_id);
            auto db_sess = repo.get_session(session_id);

            if (!db_sess) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Session not found"}}.dump(),
                    "application/json");
                return;
            }

            // Gather trade P&L for the session
            auto trades = repo.list_trades_for_strategy(db_sess->strategy_id, 10000, 0);
            std::vector<double> trade_pnl;
            for (auto& t : trades) trade_pnl.push_back(t.pnl);

            double var_1pct  = trade_pnl.empty() ? 0.0 : core::RiskEngine::compute_var(trade_pnl, 0.99);
            double sharpe    = trade_pnl.empty() ? 0.0 : core::RiskEngine::compute_sharpe(trade_pnl);
            auto equity      = core::RiskEngine::build_equity_curve(trade_pnl);
            double max_dd    = equity.empty() ? 0.0 : core::RiskEngine::compute_max_drawdown(equity);

            nlohmann::json resp = {
                {"success",       true},
                {"session_id",    session_id},
                {"status",        db_sess->status},
                {"total_pnl",     db_sess->total_pnl},
                {"var_1pct",      var_1pct},
                {"sharpe_ratio",  sharpe},
                {"max_drawdown",  max_dd},
                {"trade_count",   trades.size()},
                {"started_at",    db_sess->started_at}
            };

            if (sess) {
                resp["tick_count"] = sess->tick_count.load();
                resp["live_pnl"]   = sess->total_pnl.load();
                std::lock_guard<std::mutex> lk(sess->data_mutex);
                resp["status_msg"] = sess->status_msg;
                resp["last_error"] = sess->last_error;
            }

            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });
}

} // namespace hf::api
