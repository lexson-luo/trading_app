#include "portfolio_api.hpp"
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

void register_portfolio_routes(httplib::Server& svr,
                                db::Repository& repo,
                                const std::string& jwt_secret) {

    // GET /api/portfolio/positions
    svr.Get("/api/portfolio/positions", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);

            int64_t strategy_id = -1;
            if (!req.get_param_value("strategy_id").empty())
                strategy_id = std::stoll(req.get_param_value("strategy_id"));

            std::vector<db::PositionRow> rows;
            if (strategy_id >= 0)
                rows = repo.get_positions_for_strategy(strategy_id);
            else
                rows = repo.get_all_positions();

            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : rows) {
                arr.push_back({
                    {"id",           r.id},
                    {"strategy_id",  r.strategy_id},
                    {"symbol",       r.symbol},
                    {"net_qty",      r.net_qty},
                    {"avg_cost",     r.avg_cost},
                    {"realized_pnl", r.realized_pnl},
                    {"updated_at",   r.updated_at}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/portfolio/pnl
    svr.Get("/api/portfolio/pnl", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);

            int64_t strategy_id = -1;
            if (!req.get_param_value("strategy_id").empty())
                strategy_id = std::stoll(req.get_param_value("strategy_id"));

            std::vector<db::PositionRow> pos_rows;
            if (strategy_id >= 0)
                pos_rows = repo.get_positions_for_strategy(strategy_id);
            else
                pos_rows = repo.get_all_positions();

            // Realized P&L from positions
            double total_realized = 0.0;
            nlohmann::json pos_arr = nlohmann::json::array();
            for (auto& r : pos_rows) {
                total_realized += r.realized_pnl;
                pos_arr.push_back({
                    {"symbol",       r.symbol},
                    {"strategy_id",  r.strategy_id},
                    {"net_qty",      r.net_qty},
                    {"avg_cost",     r.avg_cost},
                    {"realized_pnl", r.realized_pnl}
                });
            }

            // Realized from trade history
            std::vector<db::TradeRow> trade_rows;
            if (strategy_id >= 0)
                trade_rows = repo.list_trades_for_strategy(strategy_id, 10000, 0);
            else
                trade_rows = repo.list_all_trades(10000, 0);

            double trade_pnl = 0.0;
            for (auto& t : trade_rows) trade_pnl += t.pnl;

            res.set_content(
                nlohmann::json{
                    {"success",             true},
                    {"realized_pnl",        total_realized},
                    {"trade_realized_pnl",  trade_pnl},
                    {"unrealized_pnl",      0.0},   // would need live prices
                    {"positions",           pos_arr}
                }.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/portfolio/trades  (paginated)
    svr.Get("/api/portfolio/trades", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);

            int limit  = 50;
            int offset = 0;
            if (!req.get_param_value("limit").empty())
                limit  = std::stoi(req.get_param_value("limit"));
            if (!req.get_param_value("offset").empty())
                offset = std::stoi(req.get_param_value("offset"));
            // Clamp
            if (limit  > 500) limit  = 500;
            if (limit  < 1)   limit  = 1;
            if (offset < 0)   offset = 0;

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
                nlohmann::json{
                    {"success", true},
                    {"data",    arr},
                    {"count",   arr.size()},
                    {"limit",   limit},
                    {"offset",  offset}
                }.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });
}

} // namespace hf::api
