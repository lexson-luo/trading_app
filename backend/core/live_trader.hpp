#pragma once
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <functional>
#include "types.hpp"
#include "../database/repository.hpp"
#include "../brokers/base.hpp"

namespace hf::core {

// Live session state shared between the worker thread and the API
struct LiveSession {
    int64_t         session_id{0};
    int64_t         strategy_id{0};
    std::string     instrument;
    StrategyType    strategy_type{StrategyType::MEAN_REVERSION};
    std::vector<StrategyParams> params_per_spread;

    std::atomic<bool>   running{false};
    std::atomic<double> total_pnl{0.0};
    std::atomic<int>    tick_count{0};

    std::string status_msg;          // last status description
    std::string last_error;
    std::string started_at;

    // Positions per symbol (spread name → net qty)
    std::map<std::string, int> spread_signals;   // last signal per spread
    mutable std::mutex         data_mutex;
};

class LiveTrader {
public:
    LiveTrader(db::Repository&   repo,
               brokers::BrokerBase& broker,
               int              tick_interval_seconds = 60,
               int              max_position_size     = 100,
               double           max_daily_loss        = 50000.0);

    ~LiveTrader();

    // Start a new live trading session.
    // Returns session_id (also persisted in DB).
    int64_t start_session(int64_t             strategy_id,
                          const std::string&  instrument,
                          StrategyType        strategy_type,
                          const std::vector<StrategyParams>& params);

    // Stop a running session by session_id.
    bool stop_session(int64_t session_id);

    // Get status of a session.
    // Returns nullptr if not found.
    std::shared_ptr<LiveSession> get_session(int64_t session_id);

    // Get all current position objects for a session.
    std::vector<Position> get_all_positions(int64_t session_id);

    // List all active session ids.
    std::vector<int64_t> active_session_ids() const;

    // Stop all sessions (called on shutdown).
    void stop_all();

private:
    db::Repository&       repo_;
    brokers::BrokerBase&  broker_;
    int                   tick_interval_s_;
    int                   max_position_size_;
    double                max_daily_loss_;

    mutable std::mutex                               sessions_mutex_;
    std::map<int64_t, std::shared_ptr<LiveSession>>  sessions_;
    std::map<int64_t, std::thread>                   threads_;

    // Thread worker for a session
    void session_worker(std::shared_ptr<LiveSession> sess);

    // Single tick: fetch data, compute signals, place orders
    void on_tick(LiveSession& sess);
};

} // namespace hf::core
