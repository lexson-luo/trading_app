#include "data_api.hpp"
#include "auth.hpp"
#include "../core/data_loader.hpp"
#include "../core/statistics.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace hf::api {

void register_data_routes(httplib::Server& svr,
                           db::Repository& repo,
                           const std::string& jwt_secret) {

    // GET /api/data/symbols
    svr.Get("/api/data/symbols", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            auto& reg = instrument_registry();
            nlohmann::json arr = nlohmann::json::array();
            for (auto& [sym, info] : reg) {
                arr.push_back({
                    {"symbol",        info.symbol},
                    {"display_name",  info.display_name},
                    {"point_value",   info.point_value},
                    {"tick_size",     info.tick_size},
                    {"currency",      info.currency},
                    {"num_contracts", info.num_contracts}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr}}.dump(),
                "application/json");
        } catch (const std::runtime_error& ex) {
            std::string msg(ex.what());
            if (msg.size() >= 3 && msg[0] == '4') {
                res.status = std::stoi(msg.substr(0,3));
                msg = msg.size() > 4 ? msg.substr(4) : msg;
            } else res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",msg}}.dump(),
                "application/json");
        }
    });

    // GET /api/data/prices?symbol=X&start=Y&end=Z
    svr.Get("/api/data/prices", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            std::string symbol = req.get_param_value("symbol");
            std::string start  = req.get_param_value("start");
            std::string end    = req.get_param_value("end");

            if (symbol.empty()) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","symbol parameter required"}}.dump(),
                    "application/json");
                return;
            }
            if (start.empty()) start = "1900-01-01";
            if (end.empty())   end   = "2099-12-31";

            auto rows = repo.get_prices(symbol, start, end);
            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : rows) {
                arr.push_back({
                    {"date",           r.date},
                    {"symbol",         r.symbol},
                    {"contract_month", r.contract_month},
                    {"price",          r.price},
                    {"volume",         r.volume}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::runtime_error& ex) {
            std::string msg(ex.what());
            if (msg.size() >= 3 && (msg[0] == '4' || msg[0] == '5')) {
                res.status = std::stoi(msg.substr(0,3));
                msg = msg.size() > 4 ? msg.substr(4) : msg;
            } else res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",msg}}.dump(),
                "application/json");
        }
    });

    // GET /api/data/spreads?instrument=X&start=Y&end=Z
    svr.Get("/api/data/spreads", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            std::string instrument = req.get_param_value("instrument");
            std::string start      = req.get_param_value("start");
            std::string end        = req.get_param_value("end");

            if (instrument.empty()) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","instrument parameter required"}}.dump(),
                    "application/json");
                return;
            }
            if (start.empty()) start = "1900-01-01";
            if (end.empty())   end   = "2099-12-31";

            auto prices  = core::DataLoader::load_prices(instrument, start, end, repo);
            auto spreads = core::DataLoader::compute_spreads(prices, instrument);

            // ADF test each spread
            for (auto& ss : spreads) {
                auto [stat, pval] = core::Statistics::adf_test(ss.values, 1);
                ss.adf_pvalue    = pval;
                ss.is_stationary = (pval < 0.05);
            }

            // EEX EUR→USD conversion
            auto& reg = instrument_registry();
            auto it = reg.find(instrument);
            if (it != reg.end() && it->second.currency == "EUR") {
                std::vector<std::string> all_dates;
                for (auto& ss : spreads)
                    for (auto& d : ss.dates) all_dates.push_back(d);
                std::sort(all_dates.begin(), all_dates.end());
                all_dates.erase(std::unique(all_dates.begin(), all_dates.end()),
                                all_dates.end());
                auto eurusd = core::DataLoader::build_eurusd_map(all_dates, repo);
                core::DataLoader::convert_eur_to_usd(spreads, eurusd);
            }

            nlohmann::json arr = nlohmann::json::array();
            for (auto& ss : spreads) {
                arr.push_back({
                    {"name",          ss.name},
                    {"dates",         ss.dates},
                    {"values",        ss.values},
                    {"is_stationary", ss.is_stationary},
                    {"adf_pvalue",    ss.adf_pvalue}
                });
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::runtime_error& ex) {
            std::string msg(ex.what());
            if (msg.size() >= 3 && (msg[0] == '4' || msg[0] == '5')) {
                res.status = std::stoi(msg.substr(0,3));
                msg = msg.size() > 4 ? msg.substr(4) : msg;
            } else res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",msg}}.dump(),
                "application/json");
        }
    });
}

} // namespace hf::api
