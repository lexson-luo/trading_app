#pragma once
#include <vector>
#include <utility>

namespace hf::core {

class RiskEngine {
public:
    // VaR (parametric, normal distribution)
    // confidence_level: e.g. 0.99 for 1% VaR, 0.95 for 5% VaR
    // Returns positive number representing the loss at given confidence
    static double compute_var(const std::vector<double>& pnl_series,
                               double confidence_level = 0.99);

    // CVaR (Expected Shortfall): average of losses beyond VaR
    static double compute_cvar(const std::vector<double>& pnl_series,
                                double var_threshold);

    // Portfolio VaR using covariance matrix (equal weights)
    // pnl_matrix: rows are time periods, cols are assets
    static double compute_portfolio_var(
        const std::vector<std::vector<double>>& pnl_matrix,
        double confidence_level = 0.99);

    // Sharpe ratio: (mean_daily_pnl / std_daily_pnl) * sqrt(trading_days)
    static double compute_sharpe(const std::vector<double>& pnl_series,
                                  int trading_days = 252);

    // Max drawdown: maximum peak-to-trough decline in equity curve
    static double compute_max_drawdown(const std::vector<double>& equity_curve);

    // Risk/reward: sum(positive pnl) / -sum(negative pnl)
    static double compute_risk_reward(const std::vector<double>& pnl_series);

    // Win rate: fraction of days with positive pnl (excluding zeros)
    static double compute_win_rate(const std::vector<double>& pnl_series);

    // Total trades: count of signal transitions (0→±1, ±1→∓1, ±1→0)
    static int count_trades(const std::vector<int>& signals);

    // Stress test: scale volatility by factor, return {stressed_var, breach_count}
    // breach_count = number of daily pnl values below normal_var
    static std::pair<double, int> stress_test(const std::vector<double>& pnl_series,
                                               double factor = 4.0);

    // Build cumulative equity curve from daily PnL
    static std::vector<double> build_equity_curve(const std::vector<double>& daily_pnl);
};

} // namespace hf::core
