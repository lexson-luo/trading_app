#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace hf {

// ── Primitive aliases ─────────────────────────────────────────────────────────
using Price    = double;          // USD (EEX already converted at load time)
using Quantity = int32_t;
using Nanos    = uint64_t;

inline Nanos now_ns() {
    using namespace std::chrono;
    return static_cast<Nanos>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}
inline std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

// ── Enumerations ──────────────────────────────────────────────────────────────
enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { MARKET = 0, LIMIT = 1 };
enum class OrderStatus : uint8_t { PENDING, FILLED, PARTIAL, CANCELLED, REJECTED };
enum class StrategyType : uint8_t { MEAN_REVERSION = 0, MOMENTUM = 1 };
enum class BacktestStatus : uint8_t { RUNNING, COMPLETED, FAILED };
enum class SessionStatus : uint8_t { RUNNING, STOPPED, ERROR };

// ── Instrument metadata ───────────────────────────────────────────────────────
struct InstrumentInfo {
    std::string symbol;           // e.g. "sgx_wmp"
    std::string display_name;     // e.g. "SGX WMP"
    double      point_value;      // USD per point
    double      tick_size;
    std::string currency;         // "USD" or "EUR" (EEX converted at load)
    int         num_contracts;    // number of monthly contracts (typically 12)
};

inline const std::map<std::string, InstrumentInfo>& instrument_registry() {
    static const std::map<std::string, InstrumentInfo> reg = {
        {"sgx_wmp",      {"sgx_wmp",      "SGX WMP",      1.0,    5.0,    "USD", 12}},
        {"sgx_smp",      {"sgx_smp",      "SGX SMP",      1.0,    5.0,    "USD", 12}},
        {"eex_smp",      {"eex_smp",      "EEX SMP",      5.0,    1.0,    "EUR", 12}},
        {"cme_nfdm",     {"cme_nfdm",     "CME NFDM",     440.0,  0.025,  "USD", 12}},
        {"cme_class_iii",{"cme_class_iii","CME Class III", 2000.0, 0.01,   "USD", 12}},
    };
    return reg;
}

// ── Price bar ─────────────────────────────────────────────────────────────────
struct PriceRow {
    std::string date;
    std::string symbol;
    int         contract_month;   // 1..12
    Price       price;
    int64_t     volume;
};

// ── Spread (M(n+1) - M(n)) ───────────────────────────────────────────────────
struct SpreadSeries {
    std::string              name;          // e.g. "sgx_wmp_spread_2_1"
    std::vector<std::string> dates;
    std::vector<double>      values;
    bool                     is_stationary{false};
    double                   adf_pvalue{1.0};
};

// ── Strategy parameters ───────────────────────────────────────────────────────
struct StrategyParams {
    int    rolling_window{10};
    double n_stdv{0.6};
    double stop_loss{1.2};
    double close_out{0.05};
    double point_value{1.0};
    int    quantity{5};
};

// ── Backtest metrics ──────────────────────────────────────────────────────────
struct BacktestMetrics {
    double total_pnl{0};
    double sharpe_ratio{0};
    double max_drawdown{0};
    double var_1pct{0};
    double var_5pct{0};
    double cvar_1pct{0};
    double cvar_5pct{0};
    double risk_reward_ratio{0};
    double win_rate{0};
    int    total_trades{0};
    int    winning_trades{0};
    // Stress test
    double stressed_var_1pct{0};
    int    stressed_breach_count{0};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(BacktestMetrics,
        total_pnl, sharpe_ratio, max_drawdown,
        var_1pct, var_5pct, cvar_1pct, cvar_5pct,
        risk_reward_ratio, win_rate, total_trades, winning_trades,
        stressed_var_1pct, stressed_breach_count)
};

// ── Per-spread backtest result ────────────────────────────────────────────────
struct SpreadResult {
    std::string     spread_name;
    StrategyParams  params;
    double          in_sample_pnl{0};
    double          out_sample_pnl{0};
    std::vector<double> daily_pnl;      // out-of-sample daily pnl
    std::vector<double> equity_curve;   // cumulative
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SpreadResult,
        spread_name, in_sample_pnl, out_sample_pnl, daily_pnl, equity_curve)
};

// ── Trade record ──────────────────────────────────────────────────────────────
struct TradeRecord {
    int64_t     id{0};
    int64_t     strategy_id{0};
    std::string symbol;
    std::string side;          // "BUY" | "SELL"
    int         quantity{0};
    Price       price{0};
    std::string order_id;
    std::string broker_order_id;
    std::string status;
    std::string timestamp;
    double      pnl{0};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TradeRecord,
        id, strategy_id, symbol, side, quantity, price,
        order_id, broker_order_id, status, timestamp, pnl)
};

// ── Position ──────────────────────────────────────────────────────────────────
struct Position {
    std::string symbol;
    int         net_qty{0};
    Price       avg_cost{0};
    Price       current_price{0};
    double      unrealized_pnl{0};
    double      realized_pnl{0};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Position,
        symbol, net_qty, avg_cost, current_price, unrealized_pnl, realized_pnl)
};

// ── API request / response models ─────────────────────────────────────────────
struct LoginRequest {
    std::string username;
    std::string password;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginRequest, username, password)
};

struct LoginResponse {
    std::string access_token;
    std::string token_type{"Bearer"};
    int         expires_in{28800};   // 8h in seconds
    std::string username;
    std::string role;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginResponse,
        access_token, token_type, expires_in, username, role)
};

struct BacktestRequest {
    int64_t     strategy_id{0};
    std::string instrument;
    std::string start_date;
    std::string end_date;
    double      cutoff{0.7};
    bool        use_custom_params{false};
    // optional custom params per spread (index → params)
    std::vector<StrategyParams> custom_params;
};

// JSON helpers for enums
inline std::string strategy_type_str(StrategyType t) {
    return t == StrategyType::MEAN_REVERSION ? "mean_reversion" : "momentum";
}
inline StrategyType strategy_type_from_str(const std::string& s) {
    return (s == "momentum") ? StrategyType::MOMENTUM : StrategyType::MEAN_REVERSION;
}

// nlohmann JSON for StrategyParams
inline void to_json(nlohmann::json& j, const StrategyParams& p) {
    j = {{"rolling_window", p.rolling_window}, {"n_stdv", p.n_stdv},
         {"stop_loss", p.stop_loss}, {"close_out", p.close_out},
         {"point_value", p.point_value}, {"quantity", p.quantity}};
}
inline void from_json(const nlohmann::json& j, StrategyParams& p) {
    j.at("rolling_window").get_to(p.rolling_window);
    j.at("n_stdv").get_to(p.n_stdv);
    j.at("stop_loss").get_to(p.stop_loss);
    j.at("close_out").get_to(p.close_out);
    if (j.contains("point_value")) j.at("point_value").get_to(p.point_value);
    if (j.contains("quantity"))    j.at("quantity").get_to(p.quantity);
}

// nlohmann JSON for BacktestRequest (manual, because custom_params is a vector<StrategyParams>)
inline void to_json(nlohmann::json& j, const BacktestRequest& r) {
    j = {
        {"strategy_id",      r.strategy_id},
        {"instrument",       r.instrument},
        {"start_date",       r.start_date},
        {"end_date",         r.end_date},
        {"cutoff",           r.cutoff},
        {"use_custom_params",r.use_custom_params},
        {"custom_params",    r.custom_params}
    };
}
inline void from_json(const nlohmann::json& j, BacktestRequest& r) {
    if (j.contains("strategy_id"))       j.at("strategy_id").get_to(r.strategy_id);
    if (j.contains("instrument"))        j.at("instrument").get_to(r.instrument);
    if (j.contains("start_date"))        j.at("start_date").get_to(r.start_date);
    if (j.contains("end_date"))          j.at("end_date").get_to(r.end_date);
    if (j.contains("cutoff"))            j.at("cutoff").get_to(r.cutoff);
    if (j.contains("use_custom_params")) j.at("use_custom_params").get_to(r.use_custom_params);
    if (j.contains("custom_params") && j.at("custom_params").is_array()) {
        r.custom_params.clear();
        for (auto& pj : j.at("custom_params"))
            r.custom_params.push_back(pj.get<StrategyParams>());
    }
}

} // namespace hf
