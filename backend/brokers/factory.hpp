#pragma once
#include <memory>
#include <string>
#include <stdexcept>
#include "base.hpp"
#include "mock_broker.hpp"
#include "rest_broker.hpp"

namespace hf::brokers {

// Factory: returns a broker instance based on mode string
// mode: "mock" → MockBroker
//       "rest" → RestBroker(url, api_key)
inline std::unique_ptr<BrokerBase> get_broker(
    const std::string& mode,
    const std::string& url     = "",
    const std::string& api_key = "") {

    if (mode == "mock") {
        auto b = std::make_unique<MockBroker>();
        b->connect();
        return b;
    } else if (mode == "rest") {
        if (url.empty()) {
            throw std::invalid_argument("RestBroker requires a non-empty broker URL");
        }
        auto b = std::make_unique<RestBroker>(url, api_key);
        b->connect();  // Attempt initial connection (failure is non-fatal)
        return b;
    } else {
        throw std::invalid_argument("Unknown broker mode: " + mode +
                                    " (expected 'mock' or 'rest')");
    }
}

} // namespace hf::brokers
