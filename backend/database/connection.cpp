#include "connection.hpp"
#include "models.hpp"
#include <memory>
#include <stdexcept>

namespace hf::db {

static std::unique_ptr<Database> g_db;

Database& get_db() {
    if (!g_db) {
        throw std::runtime_error("Database not initialized — call init_db() first");
    }
    return *g_db;
}

void init_db(const std::string& path) {
    g_db = std::make_unique<Database>(path);

    // Create all tables
    g_db->exec(SQL_CREATE_USERS);
    g_db->exec(SQL_CREATE_STRATEGIES);
    g_db->exec(SQL_CREATE_BACKTEST_RESULTS);
    g_db->exec(SQL_CREATE_TRADES);
    g_db->exec(SQL_CREATE_POSITIONS);
    g_db->exec(SQL_CREATE_LIVE_SESSIONS);
    g_db->exec(SQL_CREATE_MARKET_DATA);
    g_db->exec(SQL_CREATE_EURUSD);
}

} // namespace hf::db
