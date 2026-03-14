#include "live_trader.hpp"
#include "data_loader.hpp"
#include "strategy.hpp"
#include "types.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdexcept>

namespace hf::core {

LiveTrader::LiveTrader(db::Repository&   repo,
                       brokers::BrokerBase& broker,
                       int              tick_interval_seconds,
                       int              max_position_size,
                       double           max_daily_loss)
    : repo_(repo)
    , broker_(broker)
    , tick_interval_s_(tick_interval_seconds)
    , max_position_size_(max_position_size)
    , max_daily_loss_(max_daily_loss)
{}

LiveTrader::~LiveTrader() {
    stop_all();
}

int64_t LiveTrader::start_session(int64_t             strategy_id,
                                   const std::string&  instrument,
                                   StrategyType        strategy_type,
                                   const std::vector<StrategyParams>& params) {
    // Persist session to DB
    int64_t session_id = repo_.create_session(strategy_id);

    auto sess = std::make_shared<LiveSession>();
    sess->session_id      = session_id;
    sess->strategy_id     = strategy_id;
    sess->instrument      = instrument;
    sess->strategy_type   = strategy_type;
    sess->params_per_spread = params;
    sess->running.store(true);
    sess->started_at = now_iso();

    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        sessions_[session_id] = sess;
    }

    // Launch thread
    std::thread t([this, sess]() { session_worker(sess); });
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        threads_[sess->session_id] = std::move(t);
    }

    std::cout << "[LiveTrader] Session " << session_id
              << " started for strategy " << strategy_id
              << " instrument=" << instrument << std::endl;
    return session_id;
}

bool LiveTrader::stop_session(int64_t session_id) {
    std::shared_ptr<LiveSession> sess;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        sess = it->second;
    }
    sess->running.store(false);
    // Thread will see running==false and exit
    // Join (with timeout logic — detach if takes too long)
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = threads_.find(session_id);
        if (it != threads_.end() && it->second.joinable()) {
            it->second.detach();
            threads_.erase(it);
        }
    }
    repo_.update_session_status(session_id, "stopped", sess->total_pnl.load());
    std::cout << "[LiveTrader] Session " << session_id << " stopped" << std::endl;
    return true;
}

std::shared_ptr<LiveSession> LiveTrader::get_session(int64_t session_id) {
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    auto it = sessions_.find(session_id);
    return (it != sessions_.end()) ? it->second : nullptr;
}

std::vector<Position> LiveTrader::get_all_positions(int64_t session_id) {
    auto rows = repo_.get_positions_for_strategy(session_id);
    std::vector<Position> result;
    for (auto& r : rows) {
        Position p;
        p.symbol       = r.symbol;
        p.net_qty      = r.net_qty;
        p.avg_cost     = r.avg_cost;
        p.realized_pnl = r.realized_pnl;
        result.push_back(p);
    }
    return result;
}

std::vector<int64_t> LiveTrader::active_session_ids() const {
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    std::vector<int64_t> ids;
    for (auto& [id, sess] : sessions_) {
        if (sess->running.load()) ids.push_back(id);
    }
    return ids;
}

void LiveTrader::stop_all() {
    std::vector<int64_t> ids;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        for (auto& [id, _] : sessions_) ids.push_back(id);
    }
    for (int64_t id : ids) stop_session(id);
}

// ── Session worker thread ─────────────────────────────────────────────────────

void LiveTrader::session_worker(std::shared_ptr<LiveSession> sess) {
    std::cout << "[LiveTrader] Worker started for session " << sess->session_id << std::endl;
    while (sess->running.load()) {
        try {
            on_tick(*sess);
        } catch (const std::exception& ex) {
            std::cerr << "[LiveTrader] Session " << sess->session_id
                      << " tick error: " << ex.what() << std::endl;
            std::lock_guard<std::mutex> lk(sess->data_mutex);
            sess->last_error = ex.what();
        } catch (...) {
            std::cerr << "[LiveTrader] Session " << sess->session_id
                      << " unknown tick error" << std::endl;
        }

        // Sleep tick_interval_s_ seconds, checking running flag
        for (int i = 0; i < tick_interval_s_ * 10 && sess->running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::cout << "[LiveTrader] Worker exiting for session " << sess->session_id << std::endl;
}

void LiveTrader::on_tick(LiveSession& sess) {
    // 1. Fetch latest prices (rolling_window + buffer rows)
    int n_rows = 50; // fetch enough for the largest rolling window
    for (auto& p : sess.params_per_spread)
        n_rows = std::max(n_rows, p.rolling_window + 10);

    auto price_rows = repo_.get_recent_prices(sess.instrument, n_rows);
    if (price_rows.empty()) {
        std::lock_guard<std::mutex> lk(sess.data_mutex);
        sess.status_msg = "No price data available";
        return;
    }

    // 2. Build contract_month → [(date, price)]
    std::map<int, std::vector<std::pair<std::string, double>>> prices;
    for (auto& r : price_rows) {
        prices[r.contract_month].emplace_back(r.date, r.price);
    }

    // 3. Compute spreads
    auto spreads = DataLoader::compute_spreads(prices, sess.instrument);
    if (spreads.empty()) {
        std::lock_guard<std::mutex> lk(sess.data_mutex);
        sess.status_msg = "Cannot compute spreads";
        return;
    }

    // 4. For each spread, compute current signal and trade if changed
    double session_pnl = sess.total_pnl.load();
    int spread_idx = 0;
    for (auto& ss : spreads) {
        if (spread_idx >= static_cast<int>(sess.params_per_spread.size())) break;
        auto& params = sess.params_per_spread[spread_idx];

        int new_sig = TermSpreadStrategy::current_signal(ss.values, params, sess.strategy_type);

        int prev_sig = 0;
        {
            std::lock_guard<std::mutex> lk(sess.data_mutex);
            auto it = sess.spread_signals.find(ss.name);
            if (it != sess.spread_signals.end()) prev_sig = it->second;
        }

        if (new_sig != prev_sig) {
            // Signal changed — execute trade
            std::string symbol = sess.instrument + "_" + ss.name;

            // Close previous position if any
            if (prev_sig != 0) {
                std::string close_side = (prev_sig > 0) ? "SELL" : "BUY";
                double last_price = ss.values.empty() ? 0.0 : ss.values.back();
                std::string oid = broker_.place_order(symbol, close_side,
                                                      params.quantity, "MARKET", last_price);
                if (!oid.empty()) {
                    db::TradeRow tr;
                    tr.strategy_id = sess.strategy_id;
                    tr.symbol      = symbol;
                    tr.side        = close_side;
                    tr.quantity    = params.quantity;
                    tr.price       = last_price;
                    tr.order_id    = "live-" + std::to_string(sess.session_id);
                    tr.broker_order_id = oid;
                    tr.status      = "filled";
                    tr.timestamp   = now_iso();
                    tr.pnl         = 0.0;
                    repo_.save_trade(tr);
                }
            }

            // Open new position
            if (new_sig != 0) {
                std::string open_side = (new_sig > 0) ? "BUY" : "SELL";
                double last_price = ss.values.empty() ? 0.0 : ss.values.back();
                std::string oid = broker_.place_order(symbol, open_side,
                                                      params.quantity, "MARKET", last_price);
                if (!oid.empty()) {
                    db::TradeRow tr;
                    tr.strategy_id = sess.strategy_id;
                    tr.symbol      = symbol;
                    tr.side        = open_side;
                    tr.quantity    = params.quantity;
                    tr.price       = last_price;
                    tr.order_id    = "live-" + std::to_string(sess.session_id);
                    tr.broker_order_id = oid;
                    tr.status      = "filled";
                    tr.timestamp   = now_iso();
                    tr.pnl         = 0.0;
                    repo_.save_trade(tr);
                }
            }

            // Update signal
            {
                std::lock_guard<std::mutex> lk(sess.data_mutex);
                sess.spread_signals[ss.name] = new_sig;
            }
        }

        ++spread_idx;
    }

    // 5. Update positions in DB
    auto broker_positions = broker_.get_positions();
    for (auto& pos : broker_positions) {
        db::PositionRow pr;
        pr.strategy_id  = sess.strategy_id;
        pr.symbol       = pos.symbol;
        pr.net_qty      = pos.net_qty;
        pr.avg_cost     = pos.avg_cost;
        pr.realized_pnl = pos.realized_pnl;
        pr.updated_at   = now_iso();
        repo_.upsert_position(pr);
    }

    // 6. Risk check: if daily loss exceeds limit, stop session
    if (session_pnl < -max_daily_loss_) {
        std::cerr << "[LiveTrader] Session " << sess.session_id
                  << " hit daily loss limit (" << session_pnl
                  << " < -" << max_daily_loss_ << "). Stopping." << std::endl;
        sess.running.store(false);
        repo_.update_session_status(sess.session_id, "stopped", session_pnl);
        return;
    }

    sess.tick_count.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(sess.data_mutex);
        sess.status_msg = "Running | ticks=" + std::to_string(sess.tick_count.load()) +
                          " | pnl=" + std::to_string(sess.total_pnl.load());
    }
    repo_.update_session_status(sess.session_id, "running", sess.total_pnl.load());
}

} // namespace hf::core
