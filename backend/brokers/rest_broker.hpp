#pragma once
#include "base.hpp"
#include <string>
#include <mutex>
#include <memory>

// Forward declaration — httplib included in .cpp to keep header clean
namespace httplib { class Client; }

namespace hf::brokers {

class RestBroker : public BrokerBase {
public:
    RestBroker(const std::string& base_url, const std::string& api_key);
    ~RestBroker() override;

    bool connect()    override;
    void disconnect() override;
    bool is_connected() const override;

    std::string place_order(
        const std::string& symbol,
        const std::string& side,
        int                quantity,
        const std::string& order_type,
        double             price = 0.0) override;

    bool cancel_order(const std::string& order_id) override;

    std::vector<Position> get_positions() override;
    nlohmann::json        get_account_info() override;

private:
    std::string               base_url_;
    std::string               api_key_;
    bool                      connected_{false};
    mutable std::mutex        mutex_;
    std::unique_ptr<httplib::Client> client_;

    // Helper: make authenticated GET / POST / DELETE
    nlohmann::json http_get(const std::string& path);
    nlohmann::json http_post(const std::string& path, const nlohmann::json& body);
    bool           http_delete(const std::string& path);

    void ensure_connected();
};

} // namespace hf::brokers
