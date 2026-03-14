#include "backtest_api.hpp"
#include "auth.hpp"
#include "../core/backtester.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>

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

static nlohmann::json backtest_row_to_json(const db::BacktestResultRow& row) {
    nlohmann::json metrics_j, params_j, spreads_j;
    try { metrics_j = nlohmann::json::parse(row.metrics); } catch(...) { metrics_j = {}; }
    try { params_j  = nlohmann::json::parse(row.parameters); } catch(...) { params_j = {}; }
    try { spreads_j = nlohmann::json::parse(row.spread_results); } catch(...) { spreads_j = nlohmann::json::array(); }
    return {
        {"id",             row.id},
        {"strategy_id",    row.strategy_id},
        {"run_at",         row.run_at},
        {"instrument",     row.instrument},
        {"start_date",     row.start_date},
        {"end_date",       row.end_date},
        {"cutoff",         row.cutoff},
        {"parameters",     params_j},
        {"metrics",        metrics_j},
        {"spread_results", spreads_j},
        {"status",         row.status}
    };
}

void register_backtest_routes(httplib::Server& svr,
                               db::Repository& repo,
                               const std::string& jwt_secret) {

    // POST /api/backtest/run
    svr.Post("/api/backtest/run", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto claims = require_auth(req, jwt_secret);
            auto body   = nlohmann::json::parse(req.body);
            BacktestRequest br = body.get<BacktestRequest>();

            if (br.instrument.empty()) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","instrument required"}}.dump(),
                    "application/json");
                return;
            }
            if (!instrument_registry().count(br.instrument)) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Unknown instrument"}}.dump(),
                    "application/json");
                return;
            }

            // Resolve strategy type
            StrategyType stype = StrategyType::MEAN_REVERSION;
            if (br.strategy_id > 0) {
                auto strat = repo.get_strategy(br.strategy_id);
                if (strat) {
                    stype = strategy_type_from_str(strat->type);
                }
            }
            // Allow override via body
            if (body.contains("strategy_type")) {
                stype = strategy_type_from_str(body["strategy_type"].get<std::string>());
            }

            std::string start = br.start_date.empty() ? "1900-01-01" : br.start_date;
            std::string end   = br.end_date.empty()   ? "2099-12-31" : br.end_date;
            double cutoff     = (br.cutoff > 0.0 && br.cutoff < 1.0) ? br.cutoff : 0.70;

            const std::vector<StrategyParams>* custom_ptr = nullptr;
            if (br.use_custom_params && !br.custom_params.empty()) {
                custom_ptr = &br.custom_params;
            }

            std::cout << "[BacktestAPI] Running backtest: instrument=" << br.instrument
                      << " start=" << start << " end=" << end
                      << " cutoff=" << cutoff
                      << " type=" << strategy_type_str(stype) << std::endl;

            core::BacktestEngine engine;
            auto run_result = engine.run(br.instrument, stype, start, end,
                                          cutoff, repo, custom_ptr);

            // Serialize spread results
            nlohmann::json spreads_arr = nlohmann::json::array();
            for (auto& sr : run_result.spread_results) {
                nlohmann::json sr_j = {
                    {"spread_name",    sr.spread_name},
                    {"in_sample_pnl",  sr.in_sample_pnl},
                    {"out_sample_pnl", sr.out_sample_pnl},
                    {"daily_pnl",      sr.daily_pnl},
                    {"equity_curve",   sr.equity_curve},
                    {"params",         nlohmann::json{
                        {"rolling_window", sr.params.rolling_window},
                        {"n_stdv",         sr.params.n_stdv},
                        {"stop_loss",      sr.params.stop_loss},
                        {"close_out",      sr.params.close_out},
                        {"point_value",    sr.params.point_value},
                        {"quantity",       sr.params.quantity}
                    }}
                };
                spreads_arr.push_back(sr_j);
            }

            nlohmann::json metrics_j = run_result.metrics;

            // Save to DB
            db::BacktestResultRow db_row;
            db_row.strategy_id    = br.strategy_id;
            db_row.instrument     = br.instrument;
            db_row.start_date     = start;
            db_row.end_date       = end;
            db_row.cutoff         = cutoff;
            db_row.parameters     = body.dump();
            db_row.metrics        = metrics_j.dump();
            db_row.spread_results = spreads_arr.dump();
            db_row.status         = run_result.status;

            int64_t result_id = repo.save_backtest_result(db_row);

            nlohmann::json resp = {
                {"success",   run_result.status == "completed"},
                {"id",        result_id},
                {"status",    run_result.status},
                {"metrics",   metrics_j},
                {"spread_results", spreads_arr},
                {"portfolio_equity_curve", run_result.portfolio_equity_curve},
                {"dates",     run_result.dates}
            };
            if (run_result.status != "completed") {
                resp["error"] = run_result.error_message;
                res.status = 422;
            }
            res.set_content(resp.dump(), "application/json");

        } catch (const nlohmann::json::exception& ex) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error","JSON error: " + std::string(ex.what())}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/backtest/results
    svr.Get("/api/backtest/results", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t strategy_id = -1;
            if (!req.get_param_value("strategy_id").empty()) {
                strategy_id = std::stoll(req.get_param_value("strategy_id"));
            }
            auto rows = repo.list_backtest_results(strategy_id);
            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : rows) {
                // Lightweight list: exclude large spread_results arrays
                nlohmann::json metrics_j;
                try { metrics_j = nlohmann::json::parse(r.metrics); } catch(...) { metrics_j = {}; }
                arr.push_back({
                    {"id",          r.id},
                    {"strategy_id", r.strategy_id},
                    {"run_at",      r.run_at},
                    {"instrument",  r.instrument},
                    {"start_date",  r.start_date},
                    {"end_date",    r.end_date},
                    {"cutoff",      r.cutoff},
                    {"status",      r.status},
                    {"metrics",     metrics_j}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // GET /api/backtest/results/{id}
    svr.Get(R"(/api/backtest/results/(\d+))", [&repo, jwt_secret](
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
            res.set_content(
                nlohmann::json{{"success",true},{"data",backtest_row_to_json(*row)}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });

    // DELETE /api/backtest/results/{id}
    svr.Delete(R"(/api/backtest/results/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id = std::stoll(req.matches[1].str());
            bool ok = repo.delete_backtest_result(id);
            if (!ok) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Result not found"}}.dump(),
                    "application/json");
                return;
            }
            res.set_content(
                nlohmann::json{{"success",true},{"message","Backtest result deleted"}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error(res, ex.what());
        }
    });
}

} // namespace hf::api
