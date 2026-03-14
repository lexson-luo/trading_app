#pragma once
#include <vector>
#include <cstddef>
#include "types.hpp"

namespace hf::core {

class TermSpreadStrategy {
public:
    // Generate signals for a spread series given params and strategy type.
    // Returns vector<int>: +1=LONG, -1=SHORT, 0=FLAT
    // Length equals spread.size().
    static std::vector<int> generate_signals(
        const std::vector<double>& spread,
        const StrategyParams& params,
        StrategyType type);

    // Compute PnL: spread_diff[t] * signal[t-1] * point_value * quantity
    // Returns vector of daily PnL, same length as spread (first element = 0).
    static std::vector<double> compute_pnl(
        const std::vector<double>& spread,
        const std::vector<int>&    signals,
        double                     point_value,
        int                        quantity = 1);

    // Grid search optimization on in-sample data.
    // Tries all parameter combinations and returns best StrategyParams per spread.
    static std::vector<StrategyParams> optimize(
        const std::vector<SpreadSeries>& spreads,
        StrategyType                     type,
        size_t                           in_sample_end_idx,
        double                           point_value);

    // Get current live signal for a single spread.
    // recent_spread_values: the most recent (rolling_window + buffer) observations.
    // Returns current signal: +1, -1, or 0.
    static int current_signal(
        const std::vector<double>& recent_spread_values,
        const StrategyParams&      params,
        StrategyType               type);

    // Parameter grid (all combinations used during optimization)
    struct ParamGrid {
        std::vector<int>    rolling_windows = {5, 10, 15, 20, 25, 30};
        std::vector<double> n_stdv_values;
        std::vector<double> stop_loss_values;
        std::vector<double> close_out_values;
        ParamGrid();
    };
};

} // namespace hf::core
