#pragma once
#include <vector>
#include <utility>
#include <string>

namespace hf::core {

class Statistics {
public:
    // Rolling mean (O(n) sliding window). Values before window fills are NaN.
    static std::vector<double> rolling_mean(const std::vector<double>& data, int window);

    // Rolling std dev (population std, ddof=1 like pandas). Values before window fills are NaN.
    static std::vector<double> rolling_std(const std::vector<double>& data, int window);

    // Z-score: (x - rolling_mean) / rolling_std
    static std::vector<double> zscore(const std::vector<double>& data, int window);

    // ADF test: Augmented Dickey-Fuller with constant
    // Δy_t = α + β*y_{t-1} + Σγ_i*Δy_{t-i} + ε_t
    // Returns {adf_statistic, p_value}
    static std::pair<double, double> adf_test(const std::vector<double>& series, int lags = 1);

    // OLS regression result
    struct OLSResult {
        std::vector<double> coefficients;  // [β₀, β₁, ..., βₖ]
        std::vector<double> std_errors;    // per-coefficient
        double              r_squared{0};
        double              rss{0};        // residual sum of squares
        int                 n{0};          // number of observations
        int                 k{0};          // number of regressors (including intercept)
    };

    // OLS via Eigen: X is column-major (X[col][row]), y is response
    static OLSResult ols(const std::vector<std::vector<double>>& X,
                         const std::vector<double>& y);

    // First differences: diff[i] = series[i+1] - series[i], length = n-1
    static std::vector<double> diff(const std::vector<double>& series);

    // Mean of a vector (ignores NaN)
    static double mean(const std::vector<double>& v);

    // Standard deviation (sample, ddof=1)
    static double stddev(const std::vector<double>& v);
};

} // namespace hf::core
