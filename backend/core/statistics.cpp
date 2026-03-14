#include "statistics.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <Eigen/Dense>

namespace hf::core {

static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// ── Helpers ─────────────────────────────────────────────────────────────────

double Statistics::mean(const std::vector<double>& v) {
    double sum = 0.0;
    int cnt = 0;
    for (double x : v) {
        if (!std::isnan(x)) { sum += x; ++cnt; }
    }
    return cnt > 0 ? sum / cnt : NaN;
}

double Statistics::stddev(const std::vector<double>& v) {
    double m = mean(v);
    if (std::isnan(m)) return NaN;
    double sq = 0.0;
    int cnt = 0;
    for (double x : v) {
        if (!std::isnan(x)) { sq += (x - m) * (x - m); ++cnt; }
    }
    return cnt > 1 ? std::sqrt(sq / (cnt - 1)) : NaN;
}

// ── Rolling ──────────────────────────────────────────────────────────────────

std::vector<double> Statistics::rolling_mean(const std::vector<double>& data, int window) {
    int n = static_cast<int>(data.size());
    std::vector<double> out(n, NaN);
    if (window <= 0 || n < window) return out;

    double sum = 0.0;
    for (int i = 0; i < window; ++i) sum += data[i];
    out[window - 1] = sum / window;
    for (int i = window; i < n; ++i) {
        sum += data[i] - data[i - window];
        out[i] = sum / window;
    }
    return out;
}

std::vector<double> Statistics::rolling_std(const std::vector<double>& data, int window) {
    int n = static_cast<int>(data.size());
    std::vector<double> out(n, NaN);
    if (window <= 1 || n < window) return out;

    // Use Welford's algorithm style with sliding window
    // First window
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < window; ++i) {
        sum    += data[i];
        sum_sq += data[i] * data[i];
    }
    auto calc_std = [&](double s, double ss, int w) -> double {
        double var = (ss - s * s / w) / (w - 1);
        return var > 0.0 ? std::sqrt(var) : 0.0;
    };
    out[window - 1] = calc_std(sum, sum_sq, window);
    for (int i = window; i < n; ++i) {
        sum    += data[i] - data[i - window];
        sum_sq += data[i] * data[i] - data[i - window] * data[i - window];
        out[i] = calc_std(sum, sum_sq, window);
    }
    return out;
}

std::vector<double> Statistics::zscore(const std::vector<double>& data, int window) {
    auto mu  = rolling_mean(data, window);
    auto sig = rolling_std(data, window);
    int n = static_cast<int>(data.size());
    std::vector<double> out(n, NaN);
    for (int i = 0; i < n; ++i) {
        if (!std::isnan(mu[i]) && !std::isnan(sig[i]) && sig[i] > 1e-12) {
            out[i] = (data[i] - mu[i]) / sig[i];
        }
    }
    return out;
}

// ── Diff ─────────────────────────────────────────────────────────────────────

std::vector<double> Statistics::diff(const std::vector<double>& series) {
    if (series.size() < 2) return {};
    std::vector<double> out;
    out.reserve(series.size() - 1);
    for (size_t i = 1; i < series.size(); ++i)
        out.push_back(series[i] - series[i - 1]);
    return out;
}

// ── OLS via Eigen ────────────────────────────────────────────────────────────

Statistics::OLSResult Statistics::ols(const std::vector<std::vector<double>>& X_cols,
                                       const std::vector<double>& y) {
    OLSResult result;
    int n = static_cast<int>(y.size());
    int k = static_cast<int>(X_cols.size());  // number of columns (including intercept col)
    if (n == 0 || k == 0 || n <= k) {
        throw std::runtime_error("OLS: insufficient data");
    }
    for (auto& col : X_cols) {
        if (static_cast<int>(col.size()) != n) {
            throw std::runtime_error("OLS: column size mismatch");
        }
    }

    // Build Eigen matrices
    Eigen::MatrixXd Xm(n, k);
    Eigen::VectorXd ym(n);
    for (int j = 0; j < k; ++j)
        for (int i = 0; i < n; ++i)
            Xm(i, j) = X_cols[j][i];
    for (int i = 0; i < n; ++i)
        ym(i) = y[i];

    // β = (XᵀX)⁻¹Xᵀy  using ColPivHouseholderQR for numerical stability
    auto qr    = Xm.colPivHouseholderQr();
    Eigen::VectorXd beta = qr.solve(ym);

    Eigen::VectorXd resid = ym - Xm * beta;
    double rss   = resid.squaredNorm();
    double tss   = (ym.array() - ym.mean()).matrix().squaredNorm();
    double r2    = (tss > 1e-12) ? 1.0 - rss / tss : 0.0;

    // Standard errors: se = sqrt(s² * diag((XᵀX)⁻¹))
    double s2 = rss / (n - k);
    Eigen::MatrixXd XtX = Xm.transpose() * Xm;
    Eigen::MatrixXd XtX_inv = XtX.inverse();

    result.coefficients.resize(k);
    result.std_errors.resize(k);
    for (int j = 0; j < k; ++j) {
        result.coefficients[j] = beta(j);
        double var_j = s2 * XtX_inv(j, j);
        result.std_errors[j] = var_j > 0.0 ? std::sqrt(var_j) : 0.0;
    }
    result.r_squared = r2;
    result.rss = rss;
    result.n   = n;
    result.k   = k;
    return result;
}

// ── ADF Test ─────────────────────────────────────────────────────────────────
// Model: Δy_t = α + β*y_{t-1} + Σγ_i*Δy_{t-i} + ε_t
// t-statistic on β is the ADF statistic
// p-value approximated using MacKinnon (1994) regression-based approach

static double mackinnon_pvalue(double adf_stat, int n) {
    // MacKinnon (1994) Table 1, Case 2 (constant, no trend)
    // Critical values at 1%, 5%, 10% for n→∞:
    // cv_inf = {-3.4335, -2.8627, -2.5674}
    // b1 coefficients (finite sample correction):
    // cv_n = cv_inf + b1/n + b2/n²
    struct CV { double cv_inf, b1, b2; };
    static const CV cvs[3] = {
        {-3.4335, -5.999,  -29.25},  // 1%
        {-2.8627, -2.738,   -8.36},  // 5%
        {-2.5674, -1.438,   -4.48},  // 10%
    };
    static const double probs[3] = {0.01, 0.05, 0.10};

    double adjusted[3];
    for (int i = 0; i < 3; ++i) {
        double cv = cvs[i].cv_inf;
        if (n > 0) {
            cv += cvs[i].b1 / n;
            if (n > 1) cv += cvs[i].b2 / (n * n);
        }
        adjusted[i] = cv;
    }

    // Logistic interpolation between critical values
    // Map adf_stat to p-value using piecewise linear fit in log-odds space
    if (adf_stat <= adjusted[0]) {
        // Very significant — p very small
        // Extrapolate: each unit below cv_1pct halves p roughly
        double excess = adf_stat - adjusted[0];
        double log_p = std::log(0.01) + excess * 0.5;
        double p = std::exp(log_p);
        return std::max(1e-6, std::min(p, 1.0));
    }
    if (adf_stat >= adjusted[2]) {
        // Not significant — p > 0.10
        // Extrapolate above 10% cv
        double excess = adf_stat - adjusted[2];
        double p = 0.10 + excess * 0.15;
        return std::min(p, 1.0);
    }

    // Linear interpolation between critical values in p-space
    // Segments: [cv_1%, cv_5%] → [0.01, 0.05], [cv_5%, cv_10%] → [0.05, 0.10]
    if (adf_stat <= adjusted[1]) {
        // Between 1% and 5%
        double t = (adf_stat - adjusted[0]) / (adjusted[1] - adjusted[0]);
        return probs[0] + t * (probs[1] - probs[0]);
    } else {
        // Between 5% and 10%
        double t = (adf_stat - adjusted[1]) / (adjusted[2] - adjusted[1]);
        return probs[1] + t * (probs[2] - probs[1]);
    }
}

std::pair<double, double> Statistics::adf_test(const std::vector<double>& series, int lags) {
    int n = static_cast<int>(series.size());
    if (n < lags + 3) {
        return {0.0, 1.0};  // insufficient data → not stationary
    }

    // Compute Δy
    std::vector<double> dy = diff(series);
    int T = static_cast<int>(dy.size());  // T = n-1

    // We need: Δy_t = α + β*y_{t-1} + Σγ_i*Δy_{t-i}
    // starting from index t = lags (0-indexed in dy), i.e., t ≥ lags
    // y_{t-1} in original: series[t] (since dy[t] = series[t+1] - series[t])
    // Wait: dy[t] = series[t+1] - series[t], so y_{t-1} for dy[t] = series[t]
    // Lagged differences: Δy_{t-1} = dy[t-1], ..., Δy_{t-lags} = dy[t-lags]

    int rows = T - lags;
    if (rows < lags + 3) {
        return {0.0, 1.0};
    }

    // Build design matrix X = [1, y_{t-1}, Δy_{t-1}, ..., Δy_{t-lags}]
    // Response: Δy_t
    int ncols = 1 + 1 + lags;  // intercept + β + lags γ's
    std::vector<std::vector<double>> X(ncols, std::vector<double>(rows));
    std::vector<double> y(rows);

    for (int i = 0; i < rows; ++i) {
        int t = i + lags;  // index into dy
        X[0][i] = 1.0;                  // intercept α
        X[1][i] = series[t];            // y_{t-1} (coefficient β is what we test)
        for (int lag = 1; lag <= lags; ++lag) {
            X[1 + lag][i] = dy[t - lag]; // Δy_{t-lag}
        }
        y[i] = dy[t];
    }

    OLSResult res;
    try {
        res = ols(X, y);
    } catch (...) {
        return {0.0, 1.0};
    }

    // ADF statistic = β / se(β)
    double beta  = res.coefficients[1];
    double se    = res.std_errors[1];
    if (se < 1e-14) return {0.0, 1.0};
    double adf_stat = beta / se;

    double pvalue = mackinnon_pvalue(adf_stat, rows);
    return {adf_stat, pvalue};
}

} // namespace hf::core
