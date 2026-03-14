#include "api_client.hpp"
#include <stdexcept>

namespace hf::client {

ApiClient::ApiClient(const std::string& host, int port)
    : host_(host), port_(port) {}

void ApiClient::set_token(const std::string& token) {
    std::lock_guard<std::mutex> lk(mu_);
    token_ = token;
}

void ApiClient::clear_token() {
    std::lock_guard<std::mutex> lk(mu_);
    token_.clear();
}

bool ApiClient::has_token() const {
    std::lock_guard<std::mutex> lk(mu_);
    return !token_.empty();
}

httplib::Headers ApiClient::auth_headers() const {
    // mu_ must already be held by caller, or called internally where token_ is safe to read
    httplib::Headers headers;
    if (!token_.empty()) {
        headers.emplace("Authorization", "Bearer " + token_);
    }
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Accept", "application/json");
    return headers;
}

ApiResponse ApiClient::post(const std::string& path, const nlohmann::json& body) {
    ApiResponse resp;
    std::string token_copy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        token_copy = token_;
    }

    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(10);
        cli.set_write_timeout(10);

        httplib::Headers headers;
        if (!token_copy.empty()) {
            headers.emplace("Authorization", "Bearer " + token_copy);
        }

        std::string body_str = body.dump();
        auto res = cli.Post(path, headers, body_str, "application/json");

        if (!res) {
            resp.ok    = false;
            resp.error = "Connection failed: " + httplib::to_string(res.error());
            return resp;
        }

        resp.status = res->status;
        resp.ok     = (res->status < 400);

        if (!res->body.empty()) {
            try {
                resp.body = nlohmann::json::parse(res->body);
            } catch (const nlohmann::json::parse_error& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
                resp.body  = nlohmann::json::object();
            }
        }

        if (!resp.ok && resp.error.empty()) {
            if (resp.body.contains("detail")) {
                resp.error = resp.body["detail"].is_string()
                    ? resp.body["detail"].get<std::string>()
                    : resp.body["detail"].dump();
            } else {
                resp.error = "HTTP " + std::to_string(res->status);
            }
        }
    } catch (const std::exception& e) {
        resp.ok    = false;
        resp.error = std::string("Exception: ") + e.what();
    }

    return resp;
}

ApiResponse ApiClient::get(const std::string& path) {
    ApiResponse resp;
    std::string token_copy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        token_copy = token_;
    }

    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(10);
        cli.set_write_timeout(10);

        httplib::Headers headers;
        if (!token_copy.empty()) {
            headers.emplace("Authorization", "Bearer " + token_copy);
        }
        headers.emplace("Accept", "application/json");

        auto res = cli.Get(path, headers);

        if (!res) {
            resp.ok    = false;
            resp.error = "Connection failed: " + httplib::to_string(res.error());
            return resp;
        }

        resp.status = res->status;
        resp.ok     = (res->status < 400);

        if (!res->body.empty()) {
            try {
                resp.body = nlohmann::json::parse(res->body);
            } catch (const nlohmann::json::parse_error& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
                resp.body  = nlohmann::json::object();
            }
        }

        if (!resp.ok && resp.error.empty()) {
            if (resp.body.contains("detail")) {
                resp.error = resp.body["detail"].is_string()
                    ? resp.body["detail"].get<std::string>()
                    : resp.body["detail"].dump();
            } else {
                resp.error = "HTTP " + std::to_string(res->status);
            }
        }
    } catch (const std::exception& e) {
        resp.ok    = false;
        resp.error = std::string("Exception: ") + e.what();
    }

    return resp;
}

ApiResponse ApiClient::del(const std::string& path) {
    ApiResponse resp;
    std::string token_copy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        token_copy = token_;
    }

    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(10);
        cli.set_write_timeout(10);

        httplib::Headers headers;
        if (!token_copy.empty()) {
            headers.emplace("Authorization", "Bearer " + token_copy);
        }
        headers.emplace("Accept", "application/json");

        auto res = cli.Delete(path, headers);

        if (!res) {
            resp.ok    = false;
            resp.error = "Connection failed: " + httplib::to_string(res.error());
            return resp;
        }

        resp.status = res->status;
        resp.ok     = (res->status < 400);

        if (!res->body.empty()) {
            try {
                resp.body = nlohmann::json::parse(res->body);
            } catch (const nlohmann::json::parse_error& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
                resp.body  = nlohmann::json::object();
            }
        }

        if (!resp.ok && resp.error.empty()) {
            if (resp.body.contains("detail")) {
                resp.error = resp.body["detail"].is_string()
                    ? resp.body["detail"].get<std::string>()
                    : resp.body["detail"].dump();
            } else {
                resp.error = "HTTP " + std::to_string(res->status);
            }
        }
    } catch (const std::exception& e) {
        resp.ok    = false;
        resp.error = std::string("Exception: ") + e.what();
    }

    return resp;
}

} // namespace hf::client
