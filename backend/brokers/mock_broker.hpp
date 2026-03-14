#pragma once
#include "base.hpp"
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdint>

namespace hf::brokers {

struct MockOrder {
    std::string order_id;
    std::string symbol;
    std::string side;
    int         quantity{0};
    std::string order_type;
    double      price{0};
    double      fill_price{0};
    std::string status;  // "filled" | "cancelled"
    std::string timestamp;
};

class MockBroker : public BrokerBase {
public:
    MockBroker();
    ~MockBroker() override = default;

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

    // Access order history (for testing/inspection)
    std::vector<MockOrder> get_orders() const;

private:
    bool                           connected_{false};
    std::map<std::string, Position> positions_;  // symbol → Position
    std::vector<MockOrder>          orders_;
    mutable std::mutex              mutex_;
    std::atomic<int64_t>            next_order_id_{1};
    double                          account_balance_{1'000'000.0};

    std::string generate_order_id();
    double apply_slippage(double price, const std::string& side);
};

} // namespace hf::brokers
