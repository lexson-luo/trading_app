#include "rest_broker.hpp"
#include <httplib.h>
#include <iostream>
#include <stdexcept>

namespace hf::brokers {

RestBroker::RestBroker(const std::string& base_url, const std::string& api_key)
    : base_url_(base_url), api_key_(api_key)
{
    client_ = std::make_unique<httplib::Client>(base_url);
    client_->set_connection_timeout(5, 0);   // 5 seconds
    client_->set_read_timeout(10, 0);        // 10 seconds
}

RestBroker::~RestBroker() {
    disconnect();
}

bool RestBroker::connect() {
    std::lock_guard<std::mutex> lk(mutex_);
    // Try a simple GET /account to verify connectivity
    try {
        auto res = client_->Get("/account",
            httplib::Headers{{"Authorization", "Bearer " + api_key_}});
        if (res && (res->status == 200 || res->status == 401)) {
            // 401 means auth issue but server is reachable
            connected_ = true;
            std::cout << "[RestBroker] Connected to " << base_url_ << std::endl;
            return true;
        }
    } catch (...) {}
    std::cerr << "[RestBroker] Connection to " << base_url_ << " failed" << std::endl;
    return false;
}

void RestBroker::disconnect() {
    std::lock_guard<std::mutex> lk(mutex_);
    connected_ = false;
}

bool RestBroker::is_connected() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return connected_;
}

void RestBroker::ensure_connected() {
    if (!connected_) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!connected_) {
            // Attempt reconnect
            auto res = client_->Get("/account",
                httplib::Headers{{"Authorization", "Bearer " + api_key_}});
            if (res && res->status < 500) {
                connected_ = true;
            } else {
                throw std::runtime_error("RestBroker: not connected to " + base_url_);
            }
        }
    }
}

nlohmann::json RestBroker::http_get(const std::string& path) {
    auto res = client_->Get(path.c_str(),
        httplib::Headers{
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type", "application/json"}
        });
    if (!res) throw std::runtime_error("GET " + path + " failed: no response");
    if (res->status != 200) {
        throw std::runtime_error("GET " + path + " returned HTTP " +
                                 std::to_string(res->status) + ": " + res->body);
    }
    return nlohmann::json::parse(res->body);
}

nlohmann::json RestBroker::http_post(const std::string& path, const nlohmann::json& body) {
    std::string body_str = body.dump();
    auto res = client_->Post(path.c_str(),
        httplib::Headers{
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type",  "application/json"}
        },
        body_str, "application/json");
    if (!res) throw std::runtime_error("POST " + path + " failed: no response");
    if (res->status != 200 && res->status != 201) {
        throw std::runtime_error("POST " + path + " returned HTTP " +
                                 std::to_string(res->status) + ": " + res->body);
    }
    return nlohmann::json::parse(res->body);
}

bool RestBroker::http_delete(const std::string& path) {
    auto res = client_->Delete(path.c_str(),
        httplib::Headers{
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type", "application/json"}
        });
    if (!res) return false;
    return res->status == 200 || res->status == 204;
}

std::string RestBroker::place_order(
    const std::string& symbol,
    const std::string& side,
    int                quantity,
    const std::string& order_type,
    double             price) {

    std::lock_guard<std::mutex> lk(mutex_);
    try {
        ensure_connected();
        nlohmann::json body = {
            {"symbol",     symbol},
            {"side",       side},
            {"quantity",   quantity},
            {"order_type", order_type},
            {"price",      price}
        };
        auto resp = http_post("/orders", body);
        if (resp.contains("order_id")) {
            std::string oid = resp["order_id"].get<std::string>();
            std::cout << "[RestBroker] Placed order " << oid
                      << " " << side << " " << quantity << " " << symbol << std::endl;
            return oid;
        }
        return "";
    } catch (const std::exception& ex) {
        std::cerr << "[RestBroker] place_order error: " << ex.what() << std::endl;
        connected_ = false;  // Mark for reconnect
        return "";
    }
}

bool RestBroker::cancel_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    try {
        ensure_connected();
        return http_delete("/orders/" + order_id);
    } catch (const std::exception& ex) {
        std::cerr << "[RestBroker] cancel_order error: " << ex.what() << std::endl;
        connected_ = false;
        return false;
    }
}

std::vector<Position> RestBroker::get_positions() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Position> result;
    try {
        ensure_connected();
        auto resp = http_get("/positions");
        if (resp.is_array()) {
            for (auto& item : resp) {
                Position pos;
                pos.symbol          = item.value("symbol",        "");
                pos.net_qty         = item.value("net_qty",       0);
                pos.avg_cost        = item.value("avg_cost",      0.0);
                pos.current_price   = item.value("current_price", 0.0);
                pos.unrealized_pnl  = item.value("unrealized_pnl",0.0);
                pos.realized_pnl    = item.value("realized_pnl",  0.0);
                result.push_back(pos);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[RestBroker] get_positions error: " << ex.what() << std::endl;
        connected_ = false;
    }
    return result;
}

nlohmann::json RestBroker::get_account_info() {
    std::lock_guard<std::mutex> lk(mutex_);
    try {
        ensure_connected();
        return http_get("/account");
    } catch (const std::exception& ex) {
        std::cerr << "[RestBroker] get_account_info error: " << ex.what() << std::endl;
        connected_ = false;
        return nlohmann::json{{"error", ex.what()}};
    }
}

} // namespace hf::brokers
