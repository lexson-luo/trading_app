#include "auth.hpp"
#include "sha256.hpp"
#include "types.hpp"
#include <stdexcept>
#include <iostream>

namespace hf::api {

// ── Middleware helper ─────────────────────────────────────────────────────────

jwt::Claims require_auth(const httplib::Request& req,
                          const std::string& jwt_secret) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        throw std::runtime_error("401:Missing Authorization header");
    }
    const std::string& auth = it->second;
    if (auth.substr(0, 7) != "Bearer ") {
        throw std::runtime_error("401:Authorization must use Bearer scheme");
    }
    std::string token = auth.substr(7);
    auto claims = jwt::verify_token(jwt_secret, token);
    if (!claims.valid) {
        throw std::runtime_error("401:" + claims.error);
    }
    return claims;
}

void require_admin(const jwt::Claims& claims) {
    if (claims.role != "admin") {
        throw std::runtime_error("403:Admin role required");
    }
}

// ── Route registration ────────────────────────────────────────────────────────

void register_auth_routes(httplib::Server&   svr,
                           db::Repository&    repo,
                           const std::string& jwt_secret) {

    // POST /api/auth/login
    svr.Post("/api/auth/login", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto body = nlohmann::json::parse(req.body);
            LoginRequest lr = body.get<LoginRequest>();

            auto user_opt = repo.find_user_by_username(lr.username);
            if (!user_opt) {
                res.status = 401;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Invalid credentials"}}.dump(),
                    "application/json");
                return;
            }
            auto& user = *user_opt;
            if (!user.is_active) {
                res.status = 403;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Account disabled"}}.dump(),
                    "application/json");
                return;
            }

            std::string expected_hash = crypto::hash_password(lr.password, user.salt);
            if (expected_hash != user.password_hash) {
                res.status = 401;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","Invalid credentials"}}.dump(),
                    "application/json");
                return;
            }

            std::string token = jwt::create_token(jwt_secret, user.username, user.role);
            LoginResponse lr_resp;
            lr_resp.access_token = token;
            lr_resp.username     = user.username;
            lr_resp.role         = user.role;

            nlohmann::json resp_json = lr_resp;
            resp_json["success"] = true;
            res.set_content(resp_json.dump(), "application/json");

        } catch (const nlohmann::json::exception& ex) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error","Invalid JSON: " + std::string(ex.what())}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",ex.what()}}.dump(),
                "application/json");
        }
    });

    // POST /api/auth/register  (admin only)
    svr.Post("/api/auth/register", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto claims = require_auth(req, jwt_secret);
            require_admin(claims);

            auto body = nlohmann::json::parse(req.body);
            std::string username = body.at("username").get<std::string>();
            std::string password = body.at("password").get<std::string>();
            std::string role     = body.value("role", "trader");

            // Generate salt
            std::string salt = crypto::sha256_hex(username + now_iso());
            std::string hash = crypto::hash_password(password, salt);

            int64_t uid = repo.create_user(username, hash, salt, role);
            res.set_content(
                nlohmann::json{{"success",true},{"id",uid},{"username",username},{"role",role}}.dump(),
                "application/json");

        } catch (const std::runtime_error& ex) {
            std::string msg(ex.what());
            if (msg.substr(0,3) == "401") { res.status = 401; msg = msg.substr(4); }
            else if (msg.substr(0,3) == "403") { res.status = 403; msg = msg.substr(4); }
            else res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error",msg}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",ex.what()}}.dump(),
                "application/json");
        }
    });

    // GET /api/auth/me
    svr.Get("/api/auth/me", [&repo, jwt_secret](
        const httplib::Request& req, httplib::Response& res)
    {
        try {
            auto claims = require_auth(req, jwt_secret);
            auto user_opt = repo.find_user_by_username(claims.username);
            if (!user_opt) {
                res.status = 404;
                res.set_content(
                    nlohmann::json{{"success",false},{"error","User not found"}}.dump(),
                    "application/json");
                return;
            }
            auto& u = *user_opt;
            res.set_content(
                nlohmann::json{
                    {"success",true},
                    {"id",      u.id},
                    {"username",u.username},
                    {"role",    u.role},
                    {"created_at", u.created_at},
                    {"is_active",  u.is_active}
                }.dump(),
                "application/json");

        } catch (const std::runtime_error& ex) {
            std::string msg(ex.what());
            if (msg.substr(0,3) == "401") { res.status = 401; msg = msg.substr(4); }
            else res.status = 400;
            res.set_content(
                nlohmann::json{{"success",false},{"error",msg}}.dump(),
                "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(
                nlohmann::json{{"success",false},{"error",ex.what()}}.dump(),
                "application/json");
        }
    });
}

} // namespace hf::api
