#include "mock_broker.hpp"
#include "types.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <cmath>
#include <chrono>

namespace hf::brokers {

MockBroker::MockBroker() = default;

bool MockBroker::connect() {
    std::lock_guard<std::mutex> lk(mutex_);
    connected_ = true;
    std::cout << "[MockBroker] Connected (simulated)" << std::endl;
    return true;
}

void MockBroker::disconnect() {
    std::lock_guard<std::mutex> lk(mutex_);
    connected_ = false;
    std::cout << "[MockBroker] Disconnected" << std::endl;
}

bool MockBroker::is_connected() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return connected_;
}

std::string MockBroker::generate_order_id() {
    int64_t id = next_order_id_.fetch_add(1);
    std::ostringstream oss;
    oss << "MOCK-" << std::setfill('0') << std::setw(8) << id;
    return oss.str();
}

double MockBroker::apply_slippage(double price, const std::string& side) {
    // Random slippage ±0.05%
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(-0.0005, 0.0005);
    double slip = price * dist(rng);
    // Slippage works against the order: BUY gets higher fill, SELL gets lower
    return (side == "BUY") ? price + std::fabs(slip) : price - std::fabs(slip);
}

std::string MockBroker::place_order(
    const std::string& symbol,
    const std::string& side,
    int                quantity,
    const std::string& order_type,
    double             price) {

    std::lock_guard<std::mutex> lk(mutex_);
    if (!connected_) {
        std::cerr << "[MockBroker] Not connected — order rejected" << std::endl;
        return "";
    }

    std::string oid = generate_order_id();
    double fill_price = (price > 0.0) ? apply_slippage(price, side) : price;

    // Record order
    using namespace std::chrono;
    auto now  = system_clock::now();
    auto t    = system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));

    MockOrder order;
    order.order_id   = oid;
    order.symbol     = symbol;
    order.side       = side;
    order.quantity   = quantity;
    order.order_type = order_type;
    order.price      = price;
    order.fill_price = fill_price;
    order.status     = "filled";
    order.timestamp  = buf;
    orders_.push_back(order);

    // Update position
    auto& pos = positions_[symbol];
    pos.symbol = symbol;

    double old_qty   = pos.net_qty;
    double old_cost  = pos.avg_cost;
    double trade_qty = (side == "BUY") ? quantity : -quantity;
    double new_qty   = old_qty + trade_qty;

    double realized = 0.0;
    if (old_qty != 0 && ((old_qty > 0 && trade_qty < 0) || (old_qty < 0 && trade_qty > 0))) {
        // Closing or partial close
        double close_qty = std::min(std::fabs(old_qty), std::fabs(trade_qty));
        if (side == "SELL") {
            realized = (fill_price - old_cost) * close_qty;
        } else {
            realized = (old_cost - fill_price) * close_qty;
        }
    }

    if (new_qty == 0) {
        pos.avg_cost = 0.0;
    } else if ((old_qty >= 0 && trade_qty > 0) || (old_qty <= 0 && trade_qty < 0)) {
        // Adding to existing position
        double total_cost = old_cost * std::fabs(old_qty) + fill_price * std::fabs(trade_qty);
        pos.avg_cost = total_cost / std::fabs(new_qty);
    } else {
        // Flip or partial close — keep fill price as new avg cost
        if (std::fabs(new_qty) > 0) pos.avg_cost = fill_price;
    }
    pos.net_qty       = static_cast<int>(new_qty);
    pos.realized_pnl += realized;
    pos.current_price = fill_price;

    // Update account balance
    account_balance_ += realized;

    std::cout << "[MockBroker] Order " << oid
              << " " << side << " " << quantity << " " << symbol
              << " @ " << fill_price << " (slip from " << price << ")"
              << " realized=" << realized
              << std::endl;

    return oid;
}

bool MockBroker::cancel_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& o : orders_) {
        if (o.order_id == order_id && o.status == "filled") {
            // In mock, already filled — cannot cancel
            std::cout << "[MockBroker] Cannot cancel already-filled order " << order_id << std::endl;
            return false;
        }
        if (o.order_id == order_id) {
            o.status = "cancelled";
            std::cout << "[MockBroker] Order " << order_id << " cancelled" << std::endl;
            return true;
        }
    }
    return false;
}

std::vector<Position> MockBroker::get_positions() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Position> result;
    for (auto& [sym, pos] : positions_) {
        if (pos.net_qty != 0) result.push_back(pos);
    }
    return result;
}

nlohmann::json MockBroker::get_account_info() {
    std::lock_guard<std::mutex> lk(mutex_);
    return nlohmann::json{
        {"account_type", "mock"},
        {"balance",      account_balance_},
        {"currency",     "USD"},
        {"total_orders", orders_.size()}
    };
}

std::vector<MockOrder> MockBroker::get_orders() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return orders_;
}

} // namespace hf::brokers
