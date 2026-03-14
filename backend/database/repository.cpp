#include "repository.hpp"
#include "types.hpp"
#include <stdexcept>
#include <SQLiteCpp/SQLiteCpp.h>

namespace hf::db {

// ── Users ──────────────────────────────────────────────────────────────────

int64_t Repository::create_user(const std::string& username,
                                 const std::string& password_hash,
                                 const std::string& salt,
                                 const std::string& role) {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db,
            "INSERT INTO users (username, password_hash, salt, role, created_at, is_active) "
            "VALUES (?, ?, ?, ?, datetime('now'), 1)");
        stmt.bind(1, username);
        stmt.bind(2, password_hash);
        stmt.bind(3, salt);
        stmt.bind(4, role);
        stmt.exec();
        return db.getLastInsertRowid();
    });
}

std::optional<UserRow> Repository::find_user_by_username(const std::string& username) {
    return db_.with_db([&](SQLite::Database& db) -> std::optional<UserRow> {
        SQLite::Statement stmt(db,
            "SELECT id, username, password_hash, salt, role, created_at, is_active "
            "FROM users WHERE username = ?");
        stmt.bind(1, username);
        if (!stmt.executeStep()) return std::nullopt;
        UserRow row;
        row.id            = stmt.getColumn(0).getInt64();
        row.username      = stmt.getColumn(1).getText();
        row.password_hash = stmt.getColumn(2).getText();
        row.salt          = stmt.getColumn(3).getText();
        row.role          = stmt.getColumn(4).getText();
        row.created_at    = stmt.getColumn(5).getText();
        row.is_active     = stmt.getColumn(6).getInt() != 0;
        return row;
    });
}

bool Repository::update_user_password(const std::string& username,
                                       const std::string& new_hash,
                                       const std::string& new_salt) {
    return db_.with_db([&](SQLite::Database& db) -> bool {
        SQLite::Statement stmt(db,
            "UPDATE users SET password_hash = ?, salt = ? WHERE username = ?");
        stmt.bind(1, new_hash);
        stmt.bind(2, new_salt);
        stmt.bind(3, username);
        stmt.exec();
        return db.getChanges() > 0;
    });
}

int64_t Repository::user_count() {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db, "SELECT COUNT(*) FROM users");
        stmt.executeStep();
        return stmt.getColumn(0).getInt64();
    });
}

// ── Strategies ──────────────────────────────────────────────────────────────

int64_t Repository::create_strategy(const std::string& name,
                                     const std::string& type,
                                     const std::string& instrument,
                                     const std::string& parameters_json,
                                     const std::string& created_by) {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db,
            "INSERT INTO strategies (name, type, instrument, parameters, created_by, created_at, is_active) "
            "VALUES (?, ?, ?, ?, ?, datetime('now'), 1)");
        stmt.bind(1, name);
        stmt.bind(2, type);
        stmt.bind(3, instrument);
        stmt.bind(4, parameters_json);
        stmt.bind(5, created_by);
        stmt.exec();
        return db.getLastInsertRowid();
    });
}

std::optional<StrategyRow> Repository::get_strategy(int64_t id) {
    return db_.with_db([&](SQLite::Database& db) -> std::optional<StrategyRow> {
        SQLite::Statement stmt(db,
            "SELECT id, name, type, instrument, parameters, created_by, created_at, is_active "
            "FROM strategies WHERE id = ?");
        stmt.bind(1, static_cast<int64_t>(id));
        if (!stmt.executeStep()) return std::nullopt;
        StrategyRow row;
        row.id          = stmt.getColumn(0).getInt64();
        row.name        = stmt.getColumn(1).getText();
        row.type        = stmt.getColumn(2).getText();
        row.instrument  = stmt.getColumn(3).getText();
        row.parameters  = stmt.getColumn(4).getText();
        row.created_by  = stmt.getColumn(5).getText();
        row.created_at  = stmt.getColumn(6).getText();
        row.is_active   = stmt.getColumn(7).getInt() != 0;
        return row;
    });
}

std::vector<StrategyRow> Repository::list_strategies(bool active_only) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<StrategyRow> {
        std::string sql = "SELECT id, name, type, instrument, parameters, created_by, created_at, is_active "
                          "FROM strategies";
        if (active_only) sql += " WHERE is_active = 1";
        sql += " ORDER BY id DESC";
        SQLite::Statement stmt(db, sql);
        std::vector<StrategyRow> rows;
        while (stmt.executeStep()) {
            StrategyRow row;
            row.id         = stmt.getColumn(0).getInt64();
            row.name       = stmt.getColumn(1).getText();
            row.type       = stmt.getColumn(2).getText();
            row.instrument = stmt.getColumn(3).getText();
            row.parameters = stmt.getColumn(4).getText();
            row.created_by = stmt.getColumn(5).getText();
            row.created_at = stmt.getColumn(6).getText();
            row.is_active  = stmt.getColumn(7).getInt() != 0;
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

bool Repository::update_strategy(int64_t id,
                                  const std::string& name,
                                  const std::string& parameters_json,
                                  bool is_active) {
    return db_.with_db([&](SQLite::Database& db) -> bool {
        SQLite::Statement stmt(db,
            "UPDATE strategies SET name = ?, parameters = ?, is_active = ? WHERE id = ?");
        stmt.bind(1, name);
        stmt.bind(2, parameters_json);
        stmt.bind(3, is_active ? 1 : 0);
        stmt.bind(4, static_cast<int64_t>(id));
        stmt.exec();
        return db.getChanges() > 0;
    });
}

bool Repository::delete_strategy(int64_t id) {
    return db_.with_db([&](SQLite::Database& db) -> bool {
        SQLite::Statement stmt(db,
            "UPDATE strategies SET is_active = 0 WHERE id = ?");
        stmt.bind(1, static_cast<int64_t>(id));
        stmt.exec();
        return db.getChanges() > 0;
    });
}

// ── Backtest Results ────────────────────────────────────────────────────────

int64_t Repository::save_backtest_result(const BacktestResultRow& row) {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db,
            "INSERT INTO backtest_results "
            "(strategy_id, run_at, instrument, start_date, end_date, cutoff, parameters, metrics, spread_results, status) "
            "VALUES (?, datetime('now'), ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, static_cast<int64_t>(row.strategy_id));
        stmt.bind(2, row.instrument);
        stmt.bind(3, row.start_date);
        stmt.bind(4, row.end_date);
        stmt.bind(5, row.cutoff);
        stmt.bind(6, row.parameters);
        stmt.bind(7, row.metrics);
        stmt.bind(8, row.spread_results);
        stmt.bind(9, row.status);
        stmt.exec();
        return db.getLastInsertRowid();
    });
}

std::optional<BacktestResultRow> Repository::get_backtest_result(int64_t id) {
    return db_.with_db([&](SQLite::Database& db) -> std::optional<BacktestResultRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, run_at, instrument, start_date, end_date, "
            "cutoff, parameters, metrics, spread_results, status "
            "FROM backtest_results WHERE id = ?");
        stmt.bind(1, static_cast<int64_t>(id));
        if (!stmt.executeStep()) return std::nullopt;
        BacktestResultRow row;
        row.id             = stmt.getColumn(0).getInt64();
        row.strategy_id    = stmt.getColumn(1).getInt64();
        row.run_at         = stmt.getColumn(2).getText();
        row.instrument     = stmt.getColumn(3).getText();
        row.start_date     = stmt.getColumn(4).getText();
        row.end_date       = stmt.getColumn(5).getText();
        row.cutoff         = stmt.getColumn(6).getDouble();
        row.parameters     = stmt.getColumn(7).getText();
        row.metrics        = stmt.getColumn(8).getText();
        row.spread_results = stmt.getColumn(9).getText();
        row.status         = stmt.getColumn(10).getText();
        return row;
    });
}

std::vector<BacktestResultRow> Repository::list_backtest_results(int64_t strategy_id) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<BacktestResultRow> {
        std::string sql =
            "SELECT id, strategy_id, run_at, instrument, start_date, end_date, "
            "cutoff, parameters, metrics, spread_results, status "
            "FROM backtest_results";
        if (strategy_id >= 0) sql += " WHERE strategy_id = " + std::to_string(strategy_id);
        sql += " ORDER BY id DESC";
        SQLite::Statement stmt(db, sql);
        std::vector<BacktestResultRow> rows;
        while (stmt.executeStep()) {
            BacktestResultRow row;
            row.id             = stmt.getColumn(0).getInt64();
            row.strategy_id    = stmt.getColumn(1).getInt64();
            row.run_at         = stmt.getColumn(2).getText();
            row.instrument     = stmt.getColumn(3).getText();
            row.start_date     = stmt.getColumn(4).getText();
            row.end_date       = stmt.getColumn(5).getText();
            row.cutoff         = stmt.getColumn(6).getDouble();
            row.parameters     = stmt.getColumn(7).getText();
            row.metrics        = stmt.getColumn(8).getText();
            row.spread_results = stmt.getColumn(9).getText();
            row.status         = stmt.getColumn(10).getText();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

bool Repository::delete_backtest_result(int64_t id) {
    return db_.with_db([&](SQLite::Database& db) -> bool {
        SQLite::Statement stmt(db, "DELETE FROM backtest_results WHERE id = ?");
        stmt.bind(1, static_cast<int64_t>(id));
        stmt.exec();
        return db.getChanges() > 0;
    });
}

// ── Trades ──────────────────────────────────────────────────────────────────

int64_t Repository::save_trade(const TradeRow& row) {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db,
            "INSERT INTO trades "
            "(strategy_id, symbol, side, quantity, price, order_id, broker_order_id, status, timestamp, pnl) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, static_cast<int64_t>(row.strategy_id));
        stmt.bind(2, row.symbol);
        stmt.bind(3, row.side);
        stmt.bind(4, row.quantity);
        stmt.bind(5, row.price);
        stmt.bind(6, row.order_id);
        stmt.bind(7, row.broker_order_id);
        stmt.bind(8, row.status);
        stmt.bind(9, row.timestamp);
        stmt.bind(10, row.pnl);
        stmt.exec();
        return db.getLastInsertRowid();
    });
}

std::vector<TradeRow> Repository::list_trades_for_strategy(int64_t strategy_id,
                                                             int limit,
                                                             int offset) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<TradeRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, symbol, side, quantity, price, "
            "order_id, broker_order_id, status, timestamp, pnl "
            "FROM trades WHERE strategy_id = ? ORDER BY id DESC LIMIT ? OFFSET ?");
        stmt.bind(1, static_cast<int64_t>(strategy_id));
        stmt.bind(2, limit);
        stmt.bind(3, offset);
        std::vector<TradeRow> rows;
        while (stmt.executeStep()) {
            TradeRow row;
            row.id              = stmt.getColumn(0).getInt64();
            row.strategy_id     = stmt.getColumn(1).getInt64();
            row.symbol          = stmt.getColumn(2).getText();
            row.side            = stmt.getColumn(3).getText();
            row.quantity        = stmt.getColumn(4).getInt();
            row.price           = stmt.getColumn(5).getDouble();
            row.order_id        = stmt.getColumn(6).getText();
            row.broker_order_id = stmt.getColumn(7).getText();
            row.status          = stmt.getColumn(8).getText();
            row.timestamp       = stmt.getColumn(9).getText();
            row.pnl             = stmt.getColumn(10).getDouble();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

std::vector<TradeRow> Repository::list_all_trades(int limit, int offset) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<TradeRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, symbol, side, quantity, price, "
            "order_id, broker_order_id, status, timestamp, pnl "
            "FROM trades ORDER BY id DESC LIMIT ? OFFSET ?");
        stmt.bind(1, limit);
        stmt.bind(2, offset);
        std::vector<TradeRow> rows;
        while (stmt.executeStep()) {
            TradeRow row;
            row.id              = stmt.getColumn(0).getInt64();
            row.strategy_id     = stmt.getColumn(1).getInt64();
            row.symbol          = stmt.getColumn(2).getText();
            row.side            = stmt.getColumn(3).getText();
            row.quantity        = stmt.getColumn(4).getInt();
            row.price           = stmt.getColumn(5).getDouble();
            row.order_id        = stmt.getColumn(6).getText();
            row.broker_order_id = stmt.getColumn(7).getText();
            row.status          = stmt.getColumn(8).getText();
            row.timestamp       = stmt.getColumn(9).getText();
            row.pnl             = stmt.getColumn(10).getDouble();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

// ── Positions ────────────────────────────────────────────────────────────────

void Repository::upsert_position(const PositionRow& row) {
    db_.with_db([&](SQLite::Database& db) {
        SQLite::Statement stmt(db,
            "INSERT INTO positions (strategy_id, symbol, net_qty, avg_cost, realized_pnl, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(strategy_id, symbol) DO UPDATE SET "
            "net_qty=excluded.net_qty, avg_cost=excluded.avg_cost, "
            "realized_pnl=excluded.realized_pnl, updated_at=excluded.updated_at");
        stmt.bind(1, static_cast<int64_t>(row.strategy_id));
        stmt.bind(2, row.symbol);
        stmt.bind(3, row.net_qty);
        stmt.bind(4, row.avg_cost);
        stmt.bind(5, row.realized_pnl);
        stmt.bind(6, row.updated_at);
        stmt.exec();
        return 0;
    });
}

std::vector<PositionRow> Repository::get_positions_for_strategy(int64_t strategy_id) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<PositionRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, symbol, net_qty, avg_cost, realized_pnl, updated_at "
            "FROM positions WHERE strategy_id = ?");
        stmt.bind(1, static_cast<int64_t>(strategy_id));
        std::vector<PositionRow> rows;
        while (stmt.executeStep()) {
            PositionRow row;
            row.id           = stmt.getColumn(0).getInt64();
            row.strategy_id  = stmt.getColumn(1).getInt64();
            row.symbol       = stmt.getColumn(2).getText();
            row.net_qty      = stmt.getColumn(3).getInt();
            row.avg_cost     = stmt.getColumn(4).getDouble();
            row.realized_pnl = stmt.getColumn(5).getDouble();
            row.updated_at   = stmt.getColumn(6).getText();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

std::vector<PositionRow> Repository::get_all_positions() {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<PositionRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, symbol, net_qty, avg_cost, realized_pnl, updated_at "
            "FROM positions ORDER BY strategy_id, symbol");
        std::vector<PositionRow> rows;
        while (stmt.executeStep()) {
            PositionRow row;
            row.id           = stmt.getColumn(0).getInt64();
            row.strategy_id  = stmt.getColumn(1).getInt64();
            row.symbol       = stmt.getColumn(2).getText();
            row.net_qty      = stmt.getColumn(3).getInt();
            row.avg_cost     = stmt.getColumn(4).getDouble();
            row.realized_pnl = stmt.getColumn(5).getDouble();
            row.updated_at   = stmt.getColumn(6).getText();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

// ── Live Sessions ────────────────────────────────────────────────────────────

int64_t Repository::create_session(int64_t strategy_id) {
    return db_.with_db([&](SQLite::Database& db) -> int64_t {
        SQLite::Statement stmt(db,
            "INSERT INTO live_sessions (strategy_id, started_at, stopped_at, status, total_pnl) "
            "VALUES (?, datetime('now'), '', 'running', 0.0)");
        stmt.bind(1, static_cast<int64_t>(strategy_id));
        stmt.exec();
        return db.getLastInsertRowid();
    });
}

bool Repository::update_session_status(int64_t session_id,
                                        const std::string& status,
                                        double total_pnl) {
    return db_.with_db([&](SQLite::Database& db) -> bool {
        SQLite::Statement stmt(db,
            "UPDATE live_sessions SET status = ?, total_pnl = ?, "
            "stopped_at = CASE WHEN ? != 'running' THEN datetime('now') ELSE stopped_at END "
            "WHERE id = ?");
        stmt.bind(1, status);
        stmt.bind(2, total_pnl);
        stmt.bind(3, status);
        stmt.bind(4, static_cast<int64_t>(session_id));
        stmt.exec();
        return db.getChanges() > 0;
    });
}

std::vector<LiveSessionRow> Repository::get_active_sessions() {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<LiveSessionRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, started_at, stopped_at, status, total_pnl "
            "FROM live_sessions WHERE status = 'running'");
        std::vector<LiveSessionRow> rows;
        while (stmt.executeStep()) {
            LiveSessionRow row;
            row.id          = stmt.getColumn(0).getInt64();
            row.strategy_id = stmt.getColumn(1).getInt64();
            row.started_at  = stmt.getColumn(2).getText();
            row.stopped_at  = stmt.getColumn(3).getText();
            row.status      = stmt.getColumn(4).getText();
            row.total_pnl   = stmt.getColumn(5).getDouble();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

std::optional<LiveSessionRow> Repository::get_session(int64_t session_id) {
    return db_.with_db([&](SQLite::Database& db) -> std::optional<LiveSessionRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, started_at, stopped_at, status, total_pnl "
            "FROM live_sessions WHERE id = ?");
        stmt.bind(1, static_cast<int64_t>(session_id));
        if (!stmt.executeStep()) return std::nullopt;
        LiveSessionRow row;
        row.id          = stmt.getColumn(0).getInt64();
        row.strategy_id = stmt.getColumn(1).getInt64();
        row.started_at  = stmt.getColumn(2).getText();
        row.stopped_at  = stmt.getColumn(3).getText();
        row.status      = stmt.getColumn(4).getText();
        row.total_pnl   = stmt.getColumn(5).getDouble();
        return row;
    });
}

std::vector<LiveSessionRow> Repository::list_all_sessions() {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<LiveSessionRow> {
        SQLite::Statement stmt(db,
            "SELECT id, strategy_id, started_at, stopped_at, status, total_pnl "
            "FROM live_sessions ORDER BY id DESC LIMIT 200");
        std::vector<LiveSessionRow> rows;
        while (stmt.executeStep()) {
            LiveSessionRow row;
            row.id          = stmt.getColumn(0).getInt64();
            row.strategy_id = stmt.getColumn(1).getInt64();
            row.started_at  = stmt.getColumn(2).getText();
            row.stopped_at  = stmt.getColumn(3).getText();
            row.status      = stmt.getColumn(4).getText();
            row.total_pnl   = stmt.getColumn(5).getDouble();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

// ── Market Data ──────────────────────────────────────────────────────────────

std::vector<PriceRow> Repository::get_prices(const std::string& symbol,
                                              const std::string& start_date,
                                              const std::string& end_date) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<PriceRow> {
        SQLite::Statement stmt(db,
            "SELECT date, symbol, contract_month, price, volume "
            "FROM market_data "
            "WHERE symbol = ? AND date >= ? AND date <= ? "
            "ORDER BY date ASC, contract_month ASC");
        stmt.bind(1, symbol);
        stmt.bind(2, start_date);
        stmt.bind(3, end_date);
        std::vector<PriceRow> rows;
        while (stmt.executeStep()) {
            PriceRow row;
            row.date           = stmt.getColumn(0).getText();
            row.symbol         = stmt.getColumn(1).getText();
            row.contract_month = stmt.getColumn(2).getInt();
            row.price          = stmt.getColumn(3).getDouble();
            row.volume         = stmt.getColumn(4).getInt64();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

std::vector<PriceRow> Repository::get_recent_prices(const std::string& symbol, int n_rows) {
    return db_.with_db([&](SQLite::Database& db) -> std::vector<PriceRow> {
        // Get the most recent n_rows distinct dates
        SQLite::Statement stmt(db,
            "SELECT date, symbol, contract_month, price, volume "
            "FROM market_data "
            "WHERE symbol = ? AND date IN ("
            "  SELECT DISTINCT date FROM market_data WHERE symbol = ? "
            "  ORDER BY date DESC LIMIT ?"
            ") ORDER BY date ASC, contract_month ASC");
        stmt.bind(1, symbol);
        stmt.bind(2, symbol);
        stmt.bind(3, n_rows);
        std::vector<PriceRow> rows;
        while (stmt.executeStep()) {
            PriceRow row;
            row.date           = stmt.getColumn(0).getText();
            row.symbol         = stmt.getColumn(1).getText();
            row.contract_month = stmt.getColumn(2).getInt();
            row.price          = stmt.getColumn(3).getDouble();
            row.volume         = stmt.getColumn(4).getInt64();
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

double Repository::get_eurusd_rate(const std::string& date) {
    return db_.with_db([&](SQLite::Database& db) -> double {
        try {
            // Exact date first
            SQLite::Statement stmt(db,
                "SELECT rate FROM eurusd_rates WHERE date <= ? ORDER BY date DESC LIMIT 1");
            stmt.bind(1, date);
            if (stmt.executeStep()) {
                return stmt.getColumn(0).getDouble();
            }
        } catch (...) {}
        // Default average EUR/USD rate if not in DB
        return 1.10;
    });
}

} // namespace hf::db
