#include "risk_engine.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace hf::core {

namespace {
    // Normal distribution quantile (inverse CDF) via rational approximation
    // (Abramowitz & Stegun 26.2.17)
    double normal_quantile(double p) {
        if (p <= 0.0) return -8.0;
        if (p >= 1.0) return  8.0;
        static const double a[] = {-3.969683028665376e+01,  2.209460984245205e+02,
                                    -2.759285104469687e+02,  1.383577518672690e+02,
                                    -3.066479806614716e+01,  2.506628277459239e+00};
        static const double b[] = {-5.447609879822406e+01,  1.615858368580409e+02,
                                    -1.556989798598866e+02,  6.680131188771972e+01,
                                    -1.328068155288572e+01};
        static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                                    -2.400758277161838e+00, -2.549732539343734e+00,
                                     4.374664141464968e+00,  2.938163982698783e+00};
        static const double d[] = { 7.784695709041462e-03,  3.224671290700398e-01,
                                     2.445134137142996e+00,  3.754408661907416e+00};
        double q, r;
        if (p < 0.02425) {
            q = std::sqrt(-2.0 * std::log(p));
            return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                   ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
        } else if (p <= 0.97575) {
            q = p - 0.5;
            r = q * q;
            return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
                   (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
        } else {
            q = std::sqrt(-2.0 * std::log(1.0 - p));
            return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                    ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
        }
    }

    double vec_mean(const std::vector<double>& v) {
        if (v.empty()) return 0.0;
        double s = 0.0;
        for (double x : v) s += x;
        return s / v.size();
    }
    double vec_std(const std::vector<double>& v, double m) {
        if (v.size() < 2) return 0.0;
        double s = 0.0;
        for (double x : v) s += (x - m) * (x - m);
        return std::sqrt(s / (v.size() - 1));
    }
}

// VaR: parametric (normal)
// z_score for confidence level: 99% → 2.3263, 95% → 1.6449
double RiskEngine::compute_var(const std::vector<double>& pnl_series,
                                double confidence_level) {
    if (pnl_series.empty()) return 0.0;
    double m  = vec_mean(pnl_series);
    double sd = vec_std(pnl_series, m);
    // VaR is the loss (positive = loss), so negate
    double z = -normal_quantile(1.0 - confidence_level);  // e.g., 2.3263 for 99%
    return m - z * sd;  // negative means expected loss
}

// CVaR: average of returns below var_threshold
double RiskEngine::compute_cvar(const std::vector<double>& pnl_series,
                                 double var_threshold) {
    if (pnl_series.empty()) return 0.0;
    std::vector<double> tail;
    for (double x : pnl_series) {
        if (x < var_threshold) tail.push_back(x);
    }
    if (tail.empty()) return var_threshold;
    return vec_mean(tail);
}

// Portfolio VaR: w^T Σ w, w = 1/N
double RiskEngine::compute_portfolio_var(
    const std::vector<std::vector<double>>& pnl_matrix,
    double confidence_level) {
    if (pnl_matrix.empty()) return 0.0;
    int N = static_cast<int>(pnl_matrix.size());   // assets
    int T = static_cast<int>(pnl_matrix[0].size()); // time steps

    // Compute means
    std::vector<double> means(N, 0.0);
    for (int i = 0; i < N; ++i) means[i] = vec_mean(pnl_matrix[i]);

    // Compute covariance matrix (sample)
    std::vector<std::vector<double>> cov(N, std::vector<double>(N, 0.0));
    for (int i = 0; i < N; ++i) {
        for (int j = i; j < N; ++j) {
            double c = 0.0;
            for (int t = 0; t < T; ++t) {
                c += (pnl_matrix[i][t] - means[i]) * (pnl_matrix[j][t] - means[j]);
            }
            c /= (T > 1) ? (T - 1) : 1;
            cov[i][j] = cov[j][i] = c;
        }
    }

    // Portfolio variance = w^T Σ w with equal weights w = 1/N
    double w = 1.0 / N;
    double port_var = 0.0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            port_var += w * w * cov[i][j];

    double port_std  = port_var > 0.0 ? std::sqrt(port_var) : 0.0;
    double port_mean = 0.0;
    for (int i = 0; i < N; ++i) port_mean += w * means[i];

    double z = -normal_quantile(1.0 - confidence_level);
    return port_mean - z * port_std;
}

double RiskEngine::compute_sharpe(const std::vector<double>& pnl_series,
                                   int trading_days) {
    if (pnl_series.size() < 2) return 0.0;
    double m  = vec_mean(pnl_series);
    double sd = vec_std(pnl_series, m);
    if (sd < 1e-12) return 0.0;
    return (m / sd) * std::sqrt(static_cast<double>(trading_days));
}

double RiskEngine::compute_max_drawdown(const std::vector<double>& equity_curve) {
    if (equity_curve.empty()) return 0.0;
    double peak = equity_curve[0];
    double max_dd = 0.0;
    for (double v : equity_curve) {
        if (v > peak) peak = v;
        double dd = (peak - v);
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd;
}

double RiskEngine::compute_risk_reward(const std::vector<double>& pnl_series) {
    double pos_sum = 0.0, neg_sum = 0.0;
    for (double x : pnl_series) {
        if (x > 0.0)       pos_sum += x;
        else if (x < 0.0)  neg_sum += x;
    }
    if (neg_sum >= 0.0) return (pos_sum > 0.0) ? 999.0 : 0.0;
    return pos_sum / (-neg_sum);
}

double RiskEngine::compute_win_rate(const std::vector<double>& pnl_series) {
    int wins = 0, total = 0;
    for (double x : pnl_series) {
        if (std::fabs(x) > 1e-12) {
            ++total;
            if (x > 0.0) ++wins;
        }
    }
    return total > 0 ? static_cast<double>(wins) / total : 0.0;
}

int RiskEngine::count_trades(const std::vector<int>& signals) {
    if (signals.empty()) return 0;
    int trades = 0;
    int prev = 0;
    for (int s : signals) {
        if (s != prev) {
            if (prev != 0 || s != 0) ++trades;
        }
        prev = s;
    }
    return trades;
}

std::pair<double, int> RiskEngine::stress_test(const std::vector<double>& pnl_series,
                                                double factor) {
    if (pnl_series.empty()) return {0.0, 0};
    double normal_var = compute_var(pnl_series, 0.99);
    double m  = vec_mean(pnl_series);
    double sd = vec_std(pnl_series, m);

    // Stressed VaR: scale volatility by factor
    double z = -normal_quantile(0.01);  // 2.3263
    double stressed_var = m - z * sd * factor;

    // Count daily pnl values below (worse than) normal_var
    int breach_count = 0;
    for (double x : pnl_series) {
        if (x < normal_var) ++breach_count;
    }
    return {stressed_var, breach_count};
}

std::vector<double> RiskEngine::build_equity_curve(const std::vector<double>& daily_pnl) {
    std::vector<double> curve;
    curve.reserve(daily_pnl.size());
    double cum = 0.0;
    for (double x : daily_pnl) {
        cum += x;
        curve.push_back(cum);
    }
    return curve;
}

} // namespace hf::core
