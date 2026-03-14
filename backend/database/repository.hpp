#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "connection.hpp"
#include "types.hpp"

namespace hf::db {

// ── Row structs for DB-only fields ────────────────────────────────────────────

struct UserRow {
    int64_t     id{0};
    std::string username;
    std::string password_hash;
    std::string salt;
    std::string role;
    std::string created_at;
    bool        is_active{true};
};

struct StrategyRow {
    int64_t     id{0};
    std::string name;
    std::string type;          // "mean_reversion" | "momentum"
    std::string instrument;
    std::string parameters;    // JSON string
    std::string created_by;
    std::string created_at;
    bool        is_active{true};
};

struct BacktestResultRow {
    int64_t     id{0};
    int64_t     strategy_id{0};
    std::string run_at;
    std::string instrument;
    std::string start_date;
    std::string end_date;
    double      cutoff{0.7};
    std::string parameters;    // JSON
    std::string metrics;       // JSON (BacktestMetrics)
    std::string spread_results;// JSON array (SpreadResult[])
    std::string status;        // "completed" | "failed"
};

struct TradeRow {
    int64_t     id{0};
    int64_t     strategy_id{0};
    std::string symbol;
    std::string side;
    int         quantity{0};
    double      price{0};
    std::string order_id;
    std::string broker_order_id;
    std::string status;
    std::string timestamp;
    double      pnl{0};
};

struct PositionRow {
    int64_t     id{0};
    int64_t     strategy_id{0};
    std::string symbol;
    int         net_qty{0};
    double      avg_cost{0};
    double      realized_pnl{0};
    std::string updated_at;
};

struct LiveSessionRow {
    int64_t     id{0};
    int64_t     strategy_id{0};
    std::string started_at;
    std::string stopped_at;
    std::string status;        // "running" | "stopped" | "error"
    double      total_pnl{0};
};

// ── Repository ────────────────────────────────────────────────────────────────

class Repository {
public:
    explicit Repository(Database& db) : db_(db) {}

    // ── Users ──────────────────────────────────────────────────────────────
    int64_t             create_user(const std::string& username,
                                    const std::string& password_hash,
                                    const std::string& salt,
                                    const std::string& role = "trader");
    std::optional<UserRow> find_user_by_username(const std::string& username);
    bool                update_user_password(const std::string& username,
                                             const std::string& new_hash,
                                             const std::string& new_salt);
    int64_t             user_count();

    // ── Strategies ─────────────────────────────────────────────────────────
    int64_t                   create_strategy(const std::string& name,
                                               const std::string& type,
                                               const std::string& instrument,
                                               const std::string& parameters_json,
                                               const std::string& created_by);
    std::optional<StrategyRow> get_strategy(int64_t id);
    std::vector<StrategyRow>   list_strategies(bool active_only = true);
    bool                       update_strategy(int64_t id,
                                               const std::string& name,
                                               const std::string& parameters_json,
                                               bool is_active);
    bool                       delete_strategy(int64_t id);

    // ── Backtest Results ───────────────────────────────────────────────────
    int64_t                        save_backtest_result(const BacktestResultRow& row);
    std::optional<BacktestResultRow> get_backtest_result(int64_t id);
    std::vector<BacktestResultRow>  list_backtest_results(int64_t strategy_id = -1);
    bool                           delete_backtest_result(int64_t id);

    // ── Trades ─────────────────────────────────────────────────────────────
    int64_t               save_trade(const TradeRow& row);
    std::vector<TradeRow> list_trades_for_strategy(int64_t strategy_id,
                                                    int limit = 500,
                                                    int offset = 0);
    std::vector<TradeRow> list_all_trades(int limit = 500, int offset = 0);

    // ── Positions ──────────────────────────────────────────────────────────
    void                      upsert_position(const PositionRow& row);
    std::vector<PositionRow>  get_positions_for_strategy(int64_t strategy_id);
    std::vector<PositionRow>  get_all_positions();

    // ── Live Sessions ──────────────────────────────────────────────────────
    int64_t                      create_session(int64_t strategy_id);
    bool                         update_session_status(int64_t session_id,
                                                        const std::string& status,
                                                        double total_pnl = 0.0);
    std::vector<LiveSessionRow>  get_active_sessions();
    std::optional<LiveSessionRow> get_session(int64_t session_id);
    std::vector<LiveSessionRow>  list_all_sessions();

    // ── Market Data ────────────────────────────────────────────────────────
    // Returns rows sorted by date ASC
    std::vector<PriceRow> get_prices(const std::string& symbol,
                                      const std::string& start_date,
                                      const std::string& end_date);

    // Get most recent N rows per contract month for live trading
    std::vector<PriceRow> get_recent_prices(const std::string& symbol, int n_rows);

    // EURUSD rate lookup
    double get_eurusd_rate(const std::string& date);

private:
    Database& db_;
};

} // namespace hf::db
