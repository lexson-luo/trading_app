#pragma once
#include <string>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <SQLiteCpp/SQLiteCpp.h>

namespace hf::db {

// Thread-safe singleton database connection wrapper
class Database {
public:
    explicit Database(const std::string& path)
        : db_(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
    {
        // Enable WAL mode for concurrent reads
        db_.exec("PRAGMA journal_mode=WAL;");
        db_.exec("PRAGMA foreign_keys=ON;");
        db_.exec("PRAGMA synchronous=NORMAL;");
    }

    // Execute a raw SQL statement (DDL / simple DML)
    void exec(const std::string& sql) {
        std::lock_guard<std::mutex> lk(mutex_);
        db_.exec(sql);
    }

    // Acquire the underlying SQLite::Database for complex operations.
    // Caller must hold the mutex (use with_db()).
    SQLite::Database& raw() { return db_; }

    std::mutex& mutex() { return mutex_; }

    // Convenience: run a lambda while holding the mutex
    template<typename F>
    auto with_db(F&& f) -> decltype(std::declval<F>()(std::declval<SQLite::Database&>())) {
        std::lock_guard<std::mutex> lk(mutex_);
        return f(db_);
    }

private:
    SQLite::Database db_;
    std::mutex       mutex_;
};

// Global singleton instance
Database& get_db();

// Must be called once at startup with the db path
void init_db(const std::string& path);

} // namespace hf::db
