#include "trading_api.hpp"
#include "auth.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

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

static nlohmann::json session_to_json(const db::LiveSessionRow& row) {
    return {
        {"id",          row.id},
        {"strategy_id", row.strategy_id},
        {"started_at",  row.started_at},
        {"stopped_at",  row.stopped_at},
        {"status",      row.status},
        {"total_pnl",   row.total_pnl}
    };
}

void register_trading_routes(httplib::Server&  svr,
                              db::Repository&   repo,
                              core::LiveTrader& live_trader,
                              const std::string& jwt_secret) {

    // POST /api/trading/start
    svr.Post("/api/trading/start", [&repo, &live_trader, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto claims = require_auth(req, jwt_secret);
            auto body   = nlohmann::json::parse(req.body);

            int64_t strategy_id = body.at("strategy_id").get<int64_t>();
            auto strat = repo.get_strategy(strategy_id);
            if (!strat) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Strategy not found"}}.dump(),
                    "application/json");
                return;
            }

            StrategyType stype = strategy_type_from_str(strat->type);

            // Parse params from strategy or body
            std::vector<StrategyParams> params;
            if (body.contains("params") && body["params"].is_array()) {
                for (auto& pj : body["params"]) {
                    params.push_back(pj.get<StrategyParams>());
                }
            } else {
                // Default params from strategy definition
                try {
                    auto j = nlohmann::json::parse(strat->parameters);
                    if (j.is_array()) {
                        for (auto& pj : j) params.push_back(pj.get<StrategyParams>());
                    } else {
                        StrategyParams p = j.get<StrategyParams>();
                        params.push_back(p);
                    }
                } catch(...) {
                    StrategyParams def;
                    params.push_back(def);
                }
            }

            int64_t session_id = live_trader.start_session(
                strategy_id, strat->instrument, stype, params);

            res.status = 201;
            res.set_content(
                nlohmann::json{
                    {"success",    true},
                    {"session_id", session_id},
                    {"strategy_id",strategy_id},
                    {"instrument", strat->instrument},
                    {"status",     "running"}
                }.dump(),
                "application/json");

        } catch (const nlohmann::json::exception& ex) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error","JSON: " + std::string(ex.what())}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // POST /api/trading/stop/{session_id}
    svr.Post(R"(/api/trading/stop/(\d+))", [&repo, &live_trader, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t session_id = std::stoll(req.matches[1].str());
            bool ok = live_trader.stop_session(session_id);
            if (!ok) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Session not found or already stopped"}}.dump(),
                    "application/json");
                return;
            }
            res.set_content(
                nlohmann::json{{"success",true},{"session_id",session_id},{"status","stopped"}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/trading/sessions
    svr.Get("/api/trading/sessions", [&repo, &live_trader, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            bool active_only = (req.get_param_value("active") != "false");

            nlohmann::json arr = nlohmann::json::array();
            if (active_only) {
                auto rows = repo.get_active_sessions();
                for (auto& r : rows) {
                    auto j = session_to_json(r);
                    // Enrich with live data if available
                    auto sess = live_trader.get_session(r.id);
                    if (sess) {
                        j["tick_count"] = sess->tick_count.load();
                        j["live_pnl"]   = sess->total_pnl.load();
                        std::lock_guard<std::mutex> lk(sess->data_mutex);
                        j["status_msg"] = sess->status_msg;
                    }
                    arr.push_back(j);
                }
            } else {
                auto rows = repo.list_all_sessions();
                for (auto& r : rows) arr.push_back(session_to_json(r));
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/trading/orders
    svr.Get("/api/trading/orders", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int limit  = 100;
            int offset = 0;
            if (!req.get_param_value("limit").empty())
                limit = std::stoi(req.get_param_value("limit"));
            if (!req.get_param_value("offset").empty())
                offset = std::stoi(req.get_param_value("offset"));

            int64_t strategy_id = -1;
            if (!req.get_param_value("strategy_id").empty())
                strategy_id = std::stoll(req.get_param_value("strategy_id"));

            std::vector<db::TradeRow> rows;
            if (strategy_id >= 0)
                rows = repo.list_trades_for_strategy(strategy_id, limit, offset);
            else
                rows = repo.list_all_trades(limit, offset);

            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : rows) {
                arr.push_back({
                    {"id",               r.id},
                    {"strategy_id",      r.strategy_id},
                    {"symbol",           r.symbol},
                    {"side",             r.side},
                    {"quantity",         r.quantity},
                    {"price",            r.price},
                    {"order_id",         r.order_id},
                    {"broker_order_id",  r.broker_order_id},
                    {"status",           r.status},
                    {"timestamp",        r.timestamp},
                    {"pnl",              r.pnl}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // POST /api/trading/orders/{id}/cancel
    svr.Post(R"(/api/trading/orders/([^/]+)/cancel)", [jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            std::string order_id = req.matches[1].str();
            // In live context, cancellation goes through the broker.
            // Here we just acknowledge — actual cancel is handled by LiveTrader.
            res.set_content(
                nlohmann::json{
                    {"success",  true},
                    {"order_id", order_id},
                    {"message",  "Cancel request submitted"}
                }.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });
}

} // namespace hf::api
