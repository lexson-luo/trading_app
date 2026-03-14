#pragma once
#include <string>
#include <vector>
#include "types.hpp"
#include "../database/repository.hpp"

namespace hf::core {

class BacktestEngine {
public:
    struct RunResult {
        BacktestMetrics             metrics;
        std::vector<SpreadResult>   spread_results;
        std::vector<double>         portfolio_equity_curve;
        std::vector<std::string>    dates;
        std::string                 status;         // "completed" | "failed"
        std::string                 error_message;
    };

    // Run a full backtest:
    // 1. Load prices for instrument via DataLoader
    // 2. Compute spreads
    // 3. ADF test each spread → stationary or not
    // 4. Filter: mean_reversion uses stationary, momentum uses non-stationary
    // 5. Split at cutoff (train 70%, test 30%)
    // 6. Grid-search optimize params on train set
    // 7. Run out-of-sample PnL
    // 8. Compute risk metrics
    RunResult run(
        const std::string&              instrument,
        StrategyType                    strategy_type,
        const std::string&              start_date,
        const std::string&              end_date,
        double                          cutoff,
        db::Repository&                 repo,
        const std::vector<StrategyParams>* custom_params = nullptr);
};

} // namespace hf::core
