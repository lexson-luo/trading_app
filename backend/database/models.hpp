#pragma once
#include <string>

namespace hf::db {

// SQL schema strings — all tables

inline const char* SQL_CREATE_USERS = R"sql(
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    UNIQUE NOT NULL,
    password_hash TEXT    NOT NULL,
    salt          TEXT    NOT NULL,
    role          TEXT    NOT NULL DEFAULT 'trader',
    created_at    TEXT    NOT NULL,
    is_active     INTEGER NOT NULL DEFAULT 1
);
)sql";

inline const char* SQL_CREATE_STRATEGIES = R"sql(
CREATE TABLE IF NOT EXISTS strategies (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,
    type        TEXT    NOT NULL,
    instrument  TEXT    NOT NULL,
    parameters  TEXT    NOT NULL DEFAULT '{}',
    created_by  TEXT    NOT NULL,
    created_at  TEXT    NOT NULL,
    is_active   INTEGER NOT NULL DEFAULT 1
);
)sql";

inline const char* SQL_CREATE_BACKTEST_RESULTS = R"sql(
CREATE TABLE IF NOT EXISTS backtest_results (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id    INTEGER NOT NULL,
    run_at         TEXT    NOT NULL,
    instrument     TEXT    NOT NULL,
    start_date     TEXT    NOT NULL,
    end_date       TEXT    NOT NULL,
    cutoff         REAL    NOT NULL DEFAULT 0.7,
    parameters     TEXT    NOT NULL DEFAULT '{}',
    metrics        TEXT    NOT NULL DEFAULT '{}',
    spread_results TEXT    NOT NULL DEFAULT '[]',
    status         TEXT    NOT NULL DEFAULT 'completed'
);
)sql";

inline const char* SQL_CREATE_TRADES = R"sql(
CREATE TABLE IF NOT EXISTS trades (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id      INTEGER NOT NULL,
    symbol           TEXT    NOT NULL,
    side             TEXT    NOT NULL,
    quantity         INTEGER NOT NULL,
    price            REAL    NOT NULL,
    order_id         TEXT    NOT NULL DEFAULT '',
    broker_order_id  TEXT    NOT NULL DEFAULT '',
    status           TEXT    NOT NULL DEFAULT 'filled',
    timestamp        TEXT    NOT NULL,
    pnl              REAL    NOT NULL DEFAULT 0.0
);
)sql";

inline const char* SQL_CREATE_POSITIONS = R"sql(
CREATE TABLE IF NOT EXISTS positions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id    INTEGER NOT NULL,
    symbol         TEXT    NOT NULL,
    net_qty        INTEGER NOT NULL DEFAULT 0,
    avg_cost       REAL    NOT NULL DEFAULT 0.0,
    realized_pnl   REAL    NOT NULL DEFAULT 0.0,
    updated_at     TEXT    NOT NULL,
    UNIQUE(strategy_id, symbol)
);
)sql";

inline const char* SQL_CREATE_LIVE_SESSIONS = R"sql(
CREATE TABLE IF NOT EXISTS live_sessions (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id  INTEGER NOT NULL,
    started_at   TEXT    NOT NULL,
    stopped_at   TEXT    NOT NULL DEFAULT '',
    status       TEXT    NOT NULL DEFAULT 'running',
    total_pnl    REAL    NOT NULL DEFAULT 0.0
);
)sql";

inline const char* SQL_CREATE_MARKET_DATA = R"sql(
CREATE TABLE IF NOT EXISTS market_data (
    date           TEXT    NOT NULL,
    symbol         TEXT    NOT NULL,
    contract_month INTEGER NOT NULL,
    price          REAL    NOT NULL,
    volume         INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (date, symbol, contract_month)
);
)sql";

inline const char* SQL_CREATE_EURUSD = R"sql(
CREATE TABLE IF NOT EXISTS eurusd_rates (
    date TEXT PRIMARY KEY,
    rate REAL NOT NULL
);
)sql";

} // namespace hf::db
