#include "strategy.hpp"
#include "statistics.hpp"
#include <cmath>
#include <limits>
#include <algorithm>
#include <numeric>

namespace hf::core {

// ── Parameter Grid constructor ────────────────────────────────────────────────

TermSpreadStrategy::ParamGrid::ParamGrid() {
    // n_stdv: 0.6 to 1.1 step 0.1
    for (double v = 0.6; v <= 1.101; v += 0.1)
        n_stdv_values.push_back(std::round(v * 10) / 10.0);

    // stop_loss: 1.2 to 2.9 step 0.1
    for (double v = 1.2; v <= 2.901; v += 0.1)
        stop_loss_values.push_back(std::round(v * 10) / 10.0);

    // close_out: 0.01 to 0.13 step 0.01
    for (double v = 0.01; v <= 0.131; v += 0.01)
        close_out_values.push_back(std::round(v * 100) / 100.0);
}

// ── Signal Generation ─────────────────────────────────────────────────────────

std::vector<int> TermSpreadStrategy::generate_signals(
    const std::vector<double>& spread,
    const StrategyParams& params,
    StrategyType type) {

    int n = static_cast<int>(spread.size());
    std::vector<int> signals(n, 0);
    if (n == 0) return signals;

    auto z_scores = Statistics::zscore(spread, params.rolling_window);

    int prev_sig = 0;
    for (int i = 0; i < n; ++i) {
        double z = z_scores[i];
        if (std::isnan(z)) {
            signals[i] = prev_sig;  // hold until z-score is available
            continue;
        }

        int sig;
        if (type == StrategyType::MEAN_REVERSION) {
            // SHORT when z > n_stdv (spread too high → expect reversion down)
            // LONG  when z < -n_stdv (spread too low → expect reversion up)
            // CLOSE when |z| > stop_loss OR |z| < close_out
            if (z > params.n_stdv) {
                sig = -1;
            } else if (z < -params.n_stdv) {
                sig = 1;
            } else if (std::fabs(z) > params.stop_loss || std::fabs(z) < params.close_out) {
                sig = 0;
            } else {
                sig = prev_sig;  // hold
            }
        } else {
            // MOMENTUM
            // LONG  when z > n_stdv  (momentum up)
            // SHORT when z < -n_stdv (momentum down)
            // CLOSE when |z| < stop_loss OR |z| > close_out
            // Note: close_out here plays the role of "too far → close" threshold
            if (z > params.n_stdv) {
                sig = 1;
            } else if (z < -params.n_stdv) {
                sig = -1;
            } else if (std::fabs(z) < params.stop_loss || std::fabs(z) > params.close_out) {
                sig = 0;
            } else {
                sig = prev_sig;  // hold
            }
        }
        signals[i] = sig;
        prev_sig   = sig;
    }
    return signals;
}

// ── PnL Computation ───────────────────────────────────────────────────────────

std::vector<double> TermSpreadStrategy::compute_pnl(
    const std::vector<double>& spread,
    const std::vector<int>&    signals,
    double                     point_value,
    int                        quantity) {

    int n = static_cast<int>(spread.size());
    std::vector<double> pnl(n, 0.0);
    if (n < 2) return pnl;

    // pnl[t] = (spread[t] - spread[t-1]) * signal[t-1] * point_value * quantity
    for (int t = 1; t < n; ++t) {
        int prev_signal = signals[t - 1];
        if (prev_signal != 0) {
            double spread_diff = spread[t] - spread[t - 1];
            pnl[t] = spread_diff * prev_signal * point_value * quantity;
        }
    }
    return pnl;
}

// ── Optimization ──────────────────────────────────────────────────────────────

std::vector<StrategyParams> TermSpreadStrategy::optimize(
    const std::vector<SpreadSeries>& spreads,
    StrategyType                     type,
    size_t                           in_sample_end_idx,
    double                           point_value) {

    ParamGrid grid;
    std::vector<StrategyParams> best_params;
    best_params.reserve(spreads.size());

    for (const auto& ss : spreads) {
        // Slice to in-sample portion
        size_t end_idx = std::min(in_sample_end_idx, ss.values.size());
        if (end_idx < 10) {
            // Not enough data — use defaults
            StrategyParams def;
            def.point_value = point_value;
            best_params.push_back(def);
            continue;
        }
        std::vector<double> in_sample(ss.values.begin(),
                                      ss.values.begin() + end_idx);

        double best_pnl = -std::numeric_limits<double>::infinity();
        StrategyParams best;
        best.point_value = point_value;

        for (int w : grid.rolling_windows) {
            for (double ns : grid.n_stdv_values) {
                for (double sl : grid.stop_loss_values) {
                    for (double co : grid.close_out_values) {
                        StrategyParams p;
                        p.rolling_window = w;
                        p.n_stdv         = ns;
                        p.stop_loss      = sl;
                        p.close_out      = co;
                        p.point_value    = point_value;
                        p.quantity       = 1;

                        auto sigs = generate_signals(in_sample, p, type);
                        auto pnl  = compute_pnl(in_sample, sigs, point_value, 1);

                        double total = 0.0;
                        for (double x : pnl) total += x;

                        if (total > best_pnl) {
                            best_pnl = total;
                            best     = p;
                        }
                    }
                }
            }
        }
        best_params.push_back(best);
    }
    return best_params;
}

// ── Current Live Signal ───────────────────────────────────────────────────────

int TermSpreadStrategy::current_signal(
    const std::vector<double>& recent_spread_values,
    const StrategyParams&      params,
    StrategyType               type) {

    if (recent_spread_values.empty()) return 0;

    auto z_scores = Statistics::zscore(recent_spread_values, params.rolling_window);
    // Take the last valid z-score
    double z = std::numeric_limits<double>::quiet_NaN();
    for (int i = static_cast<int>(z_scores.size()) - 1; i >= 0; --i) {
        if (!std::isnan(z_scores[i])) {
            z = z_scores[i];
            break;
        }
    }
    if (std::isnan(z)) return 0;

    if (type == StrategyType::MEAN_REVERSION) {
        if (z > params.n_stdv)       return -1;
        if (z < -params.n_stdv)      return  1;
        if (std::fabs(z) > params.stop_loss || std::fabs(z) < params.close_out)
            return 0;
    } else {
        if (z > params.n_stdv)       return  1;
        if (z < -params.n_stdv)      return -1;
        if (std::fabs(z) < params.stop_loss || std::fabs(z) > params.close_out)
            return 0;
    }
    // Hold — return 0 (no previous context for live single-point call)
    return 0;
}

} // namespace hf::core
