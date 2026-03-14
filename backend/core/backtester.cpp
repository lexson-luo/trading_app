#include "backtester.hpp"
#include "data_loader.hpp"
#include "statistics.hpp"
#include "strategy.hpp"
#include "risk_engine.hpp"
#include "types.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace hf::core {

BacktestEngine::RunResult BacktestEngine::run(
    const std::string&               instrument,
    StrategyType                     strategy_type,
    const std::string&               start_date,
    const std::string&               end_date,
    double                           cutoff,
    db::Repository&                  repo,
    const std::vector<StrategyParams>* custom_params) {

    RunResult result;
    result.status = "completed";

    try {
        // ── 1. Load prices ───────────────────────────────────────────────────
        auto prices = DataLoader::load_prices(instrument, start_date, end_date, repo);
        if (prices.empty()) {
            result.status        = "failed";
            result.error_message = "No price data found for " + instrument +
                                   " between " + start_date + " and " + end_date;
            return result;
        }

        // ── 2. Compute spreads ───────────────────────────────────────────────
        auto spreads = DataLoader::compute_spreads(prices, instrument);
        if (spreads.empty()) {
            result.status        = "failed";
            result.error_message = "Could not compute spreads — need at least 2 contract months";
            return result;
        }

        // For EEX SMP, convert EUR → USD
        auto& reg = instrument_registry();
        auto it = reg.find(instrument);
        double point_value = 1.0;
        if (it != reg.end()) {
            point_value = it->second.point_value;
            if (it->second.currency == "EUR") {
                // Build EUR/USD map from all spread dates
                std::vector<std::string> all_dates;
                for (auto& ss : spreads)
                    for (auto& d : ss.dates) all_dates.push_back(d);
                std::sort(all_dates.begin(), all_dates.end());
                all_dates.erase(std::unique(all_dates.begin(), all_dates.end()),
                                all_dates.end());
                auto eurusd = DataLoader::build_eurusd_map(all_dates, repo);
                DataLoader::convert_eur_to_usd(spreads, eurusd);
            }
        }

        // ── 3. ADF test each spread ──────────────────────────────────────────
        for (auto& ss : spreads) {
            auto [adf_stat, pvalue] = Statistics::adf_test(ss.values, 1);
            ss.adf_pvalue    = pvalue;
            ss.is_stationary = (pvalue < 0.05);
        }

        // ── 4. Filter spreads by strategy type ──────────────────────────────
        std::vector<SpreadSeries*> active_spreads;
        for (auto& ss : spreads) {
            if (strategy_type == StrategyType::MEAN_REVERSION && ss.is_stationary)
                active_spreads.push_back(&ss);
            else if (strategy_type == StrategyType::MOMENTUM && !ss.is_stationary)
                active_spreads.push_back(&ss);
        }

        // If no spreads pass the filter, use all (fallback)
        if (active_spreads.empty()) {
            for (auto& ss : spreads) active_spreads.push_back(&ss);
        }

        // ── 5. Train/test split ──────────────────────────────────────────────
        // Use the first active spread's length as reference
        size_t total_len     = active_spreads[0]->values.size();
        size_t train_end_idx = static_cast<size_t>(std::round(cutoff * total_len));
        if (train_end_idx < 10) train_end_idx = std::min<size_t>(10, total_len);

        // ── 6. Optimize or use custom params ────────────────────────────────
        std::vector<StrategyParams> params_per_spread;
        if (custom_params && !custom_params->empty()) {
            for (size_t i = 0; i < active_spreads.size(); ++i) {
                if (i < custom_params->size()) {
                    auto p = (*custom_params)[i];
                    p.point_value = point_value;
                    params_per_spread.push_back(p);
                } else {
                    StrategyParams def;
                    def.point_value = point_value;
                    params_per_spread.push_back(def);
                }
            }
        } else {
            // Build vector of SpreadSeries for optimize()
            std::vector<SpreadSeries> active_vec;
            for (auto* p : active_spreads) active_vec.push_back(*p);
            params_per_spread = TermSpreadStrategy::optimize(
                active_vec, strategy_type, train_end_idx, point_value);
        }

        // ── 7. Out-of-sample run ─────────────────────────────────────────────
        // Collect portfolio-level daily PnL (summed across spreads)
        // Use the common dates of the out-of-sample window
        std::vector<double> portfolio_pnl;
        std::vector<std::string> oos_dates;

        for (size_t s_idx = 0; s_idx < active_spreads.size(); ++s_idx) {
            auto& ss   = *active_spreads[s_idx];
            auto& p    = params_per_spread[s_idx];
            size_t len = ss.values.size();

            // In-sample PnL
            std::vector<double> in_sample(ss.values.begin(),
                                          ss.values.begin() + std::min(train_end_idx, len));
            auto in_sigs   = TermSpreadStrategy::generate_signals(in_sample, p, strategy_type);
            auto in_pnl    = TermSpreadStrategy::compute_pnl(in_sample, in_sigs, p.point_value, p.quantity);
            double in_total = 0.0;
            for (double x : in_pnl) in_total += x;

            // Out-of-sample PnL
            size_t oos_start = train_end_idx > 0 ? train_end_idx - 1 : 0; // include one row for first diff
            std::vector<double> oos_values(ss.values.begin() + std::min(oos_start, len),
                                           ss.values.end());
            std::vector<std::string> oos_dates_s;
            if (oos_start < ss.dates.size()) {
                oos_dates_s = std::vector<std::string>(
                    ss.dates.begin() + oos_start, ss.dates.end());
            }

            auto oos_sigs   = TermSpreadStrategy::generate_signals(oos_values, p, strategy_type);
            auto oos_pnl    = TermSpreadStrategy::compute_pnl(oos_values, oos_sigs,
                                                               p.point_value, p.quantity);
            double oos_total = 0.0;
            for (double x : oos_pnl) oos_total += x;

            // Build equity curve
            auto equity = RiskEngine::build_equity_curve(oos_pnl);

            SpreadResult sr;
            sr.spread_name    = ss.name;
            sr.params         = p;
            sr.in_sample_pnl  = in_total;
            sr.out_sample_pnl = oos_total;
            sr.daily_pnl      = oos_pnl;
            sr.equity_curve   = equity;
            result.spread_results.push_back(std::move(sr));

            // Add to portfolio PnL (align by index)
            if (portfolio_pnl.empty()) {
                portfolio_pnl = oos_pnl;
                oos_dates     = oos_dates_s;
            } else {
                size_t min_len = std::min(portfolio_pnl.size(), oos_pnl.size());
                for (size_t i = 0; i < min_len; ++i) {
                    portfolio_pnl[i] += oos_pnl[i];
                }
                if (oos_pnl.size() > portfolio_pnl.size())
                    portfolio_pnl.resize(oos_pnl.size(), 0.0);
            }
        }

        if (portfolio_pnl.empty()) {
            result.status        = "failed";
            result.error_message = "No out-of-sample data to evaluate";
            return result;
        }

        result.dates                 = oos_dates;
        result.portfolio_equity_curve = RiskEngine::build_equity_curve(portfolio_pnl);

        // ── 8. Compute metrics ────────────────────────────────────────────────
        BacktestMetrics m;
        m.total_pnl = 0.0;
        for (double x : portfolio_pnl) m.total_pnl += x;

        m.sharpe_ratio     = RiskEngine::compute_sharpe(portfolio_pnl);
        m.max_drawdown     = RiskEngine::compute_max_drawdown(result.portfolio_equity_curve);
        m.var_1pct         = RiskEngine::compute_var(portfolio_pnl, 0.99);
        m.var_5pct         = RiskEngine::compute_var(portfolio_pnl, 0.95);
        m.cvar_1pct        = RiskEngine::compute_cvar(portfolio_pnl, m.var_1pct);
        m.cvar_5pct        = RiskEngine::compute_cvar(portfolio_pnl, m.var_5pct);
        m.risk_reward_ratio = RiskEngine::compute_risk_reward(portfolio_pnl);
        m.win_rate         = RiskEngine::compute_win_rate(portfolio_pnl);

        // Count total trades across all spreads
        int total_trades = 0, winning_trades = 0;
        for (auto& sr : result.spread_results) {
            auto sigs = TermSpreadStrategy::generate_signals(
                sr.daily_pnl.size() > 0 ? std::vector<double>(
                    active_spreads[0]->values.begin() +
                    std::min(train_end_idx > 0 ? train_end_idx - 1 : 0,
                             active_spreads[0]->values.size()),
                    active_spreads[0]->values.end()) : sr.daily_pnl,
                sr.params, strategy_type);
            total_trades   += RiskEngine::count_trades(sigs);
            int wins = 0;
            for (double x : sr.daily_pnl)
                if (x > 0.0) ++wins;
            winning_trades += wins;
        }
        m.total_trades   = total_trades;
        m.winning_trades = winning_trades;

        auto [sv, bc]   = RiskEngine::stress_test(portfolio_pnl, 4.0);
        m.stressed_var_1pct     = sv;
        m.stressed_breach_count = bc;

        result.metrics = m;

    } catch (const std::exception& ex) {
        result.status        = "failed";
        result.error_message = ex.what();
    } catch (...) {
        result.status        = "failed";
        result.error_message = "Unknown error during backtest";
    }

    return result;
}

} // namespace hf::core
