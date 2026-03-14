#include "strategy_api.hpp"
#include "auth.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace hf::api {

// Helper: parse HTTP status prefix from runtime_error messages
static void set_error_response(httplib::Response& res, const std::string& msg_raw,
                                 int default_status = 500) {
    std::string msg = msg_raw;
    int status = default_status;
    if (msg.size() >= 4 && (msg[0]=='4'||msg[0]=='5') && msg[3]==':') {
        try { status = std::stoi(msg.substr(0,3)); } catch(...) {}
        msg = msg.substr(4);
    }
    res.status = status;
    res.set_content(
        nlohmann::json{{"success",false},{"error",msg}}.dump(),
        "application/json");
}

static nlohmann::json strategy_row_to_json(const db::StrategyRow& row) {
    nlohmann::json params_json;
    try { params_json = nlohmann::json::parse(row.parameters); }
    catch(...) { params_json = nlohmann::json::object(); }
    return {
        {"id",          row.id},
        {"name",        row.name},
        {"type",        row.type},
        {"instrument",  row.instrument},
        {"parameters",  params_json},
        {"created_by",  row.created_by},
        {"created_at",  row.created_at},
        {"is_active",   row.is_active}
    };
}

void register_strategy_routes(httplib::Server& svr,
                                db::Repository& repo,
                                const std::string& jwt_secret) {

    // GET /api/strategies
    svr.Get("/api/strategies", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            bool active_only = (req.get_param_value("all") != "true");
            auto rows = repo.list_strategies(active_only);
            nlohmann::json arr = nlohmann::json::array();
            for (auto& r : rows) arr.push_back(strategy_row_to_json(r));
            res.set_content(
                nlohmann::json{{"success",true},{"data",arr},{"count",arr.size()}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error_response(res, ex.what());
        }
    });

    // POST /api/strategies
    svr.Post("/api/strategies", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto claims = require_auth(req, jwt_secret);
            auto body   = nlohmann::json::parse(req.body);

            std::string name       = body.at("name").get<std::string>();
            std::string type       = body.at("type").get<std::string>();
            std::string instrument = body.at("instrument").get<std::string>();
            std::string params_str = body.contains("parameters")
                ? body["parameters"].dump() : "{}";

            // Validate type
            if (type != "mean_reversion" && type != "momentum") {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},
                        {"error","type must be 'mean_reversion' or 'momentum'"}}.dump(),
                    "application/json");
                return;
            }
            // Validate instrument
            if (!instrument_registry().count(instrument)) {
                res.status = 400;
                res.set_content(
                    nlohmann::json{{"success",false},
                        {"error","Unknown instrument: " + instrument}}.dump(),
                    "application/json");
                return;
            }

            int64_t id = repo.create_strategy(name, type, instrument, params_str,
                                               claims.username);
            auto row = repo.get_strategy(id);
            res.status = 201;
            res.set_content(
                nlohmann::json{{"success",true},
                    {"data", row ? strategy_row_to_json(*row) : nlohmann::json{}}}.dump(),
                "application/json");
        } catch (const nlohmann::json::exception& ex) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error","JSON error: " + std::string(ex.what())}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error_response(res, ex.what());
        }
    });

    // GET /api/strategies/{id}
    svr.Get(R"(/api/strategies/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id = std::stoll(req.matches[1].str());
            auto row = repo.get_strategy(id);
            if (!row) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Strategy not found"}}.dump(),
                    "application/json");
                return;
            }
            res.set_content(
                nlohmann::json{{"success",true},{"data",strategy_row_to_json(*row)}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error_response(res, ex.what());
        }
    });

    // PUT /api/strategies/{id}
    svr.Put(R"(/api/strategies/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id   = std::stoll(req.matches[1].str());
            auto body    = nlohmann::json::parse(req.body);

            auto existing = repo.get_strategy(id);
            if (!existing) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Strategy not found"}}.dump(),
                    "application/json");
                return;
            }

            std::string name   = body.value("name",       existing->name);
            bool is_active     = body.value("is_active",  existing->is_active);
            std::string params_str = body.contains("parameters")
                ? body["parameters"].dump() : existing->parameters;

            bool ok = repo.update_strategy(id, name, params_str, is_active);
            auto updated = repo.get_strategy(id);
            res.set_content(
                nlohmann::json{{"success",ok},
                    {"data", updated ? strategy_row_to_json(*updated) : nlohmann::json{}}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error_response(res, ex.what());
        }
    });

    // DELETE /api/strategies/{id}
    svr.Delete(R"(/api/strategies/(\d+))", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            require_auth(req, jwt_secret);
            int64_t id = std::stoll(req.matches[1].str());
            bool ok = repo.delete_strategy(id);
            if (!ok) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Strategy not found"}}.dump(),
                    "application/json");
                return;
            }
            res.set_content(
                nlohmann::json{{"success",true},{"message","Strategy deactivated"}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            set_error_response(res, ex.what());
        }
    });
}

} // namespace hf::api
