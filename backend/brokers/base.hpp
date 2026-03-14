#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "types.hpp"

namespace hf::brokers {

class BrokerBase {
public:
    virtual ~BrokerBase() = default;

    // Connection lifecycle
    virtual bool connect()    = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Order management
    // Returns broker-assigned order id on success, empty string on failure
    virtual std::string place_order(
        const std::string& symbol,
        const std::string& side,     // "BUY" | "SELL"
        int                quantity,
        const std::string& order_type, // "MARKET" | "LIMIT"
        double             price = 0.0) = 0;

    virtual bool cancel_order(const std::string& order_id) = 0;

    // Portfolio queries
    virtual std::vector<Position> get_positions() = 0;
    virtual nlohmann::json        get_account_info() = 0;
};

} // namespace hf::brokers
