#include "data_loader.hpp"
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace hf::core {

std::map<int, std::vector<std::pair<std::string, double>>>
DataLoader::load_prices(const std::string& instrument,
                         const std::string& start_date,
                         const std::string& end_date,
                         db::Repository& repo) {
    auto rows = repo.get_prices(instrument, start_date, end_date);
    std::map<int, std::vector<std::pair<std::string, double>>> result;
    for (auto& r : rows) {
        result[r.contract_month].emplace_back(r.date, r.price);
    }
    // Ensure sorted by date within each month (should already be from SQL ORDER BY)
    for (auto& [month, vec] : result) {
        std::stable_sort(vec.begin(), vec.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    return result;
}

std::vector<SpreadSeries>
DataLoader::compute_spreads(
    const std::map<int, std::vector<std::pair<std::string, double>>>& prices,
    const std::string& instrument) {

    std::vector<SpreadSeries> spreads;

    // Spreads: spread_(n+1)_n = M(n+1) - M(n) for n = 1..11
    for (int n = 1; n <= 11; ++n) {
        int m_near = n;       // M(n)
        int m_far  = n + 1;   // M(n+1)

        auto it_near = prices.find(m_near);
        auto it_far  = prices.find(m_far);
        if (it_near == prices.end() || it_far == prices.end()) continue;

        const auto& near_vec = it_near->second;
        const auto& far_vec  = it_far->second;

        // Build date → price maps for each leg
        std::map<std::string, double> near_map, far_map;
        for (auto& [d, p] : near_vec) near_map[d] = p;
        for (auto& [d, p] : far_vec)  far_map[d]  = p;

        // Find common dates
        std::vector<std::string> common_dates;
        for (auto& [d, _] : near_map) {
            if (far_map.count(d)) common_dates.push_back(d);
        }
        std::sort(common_dates.begin(), common_dates.end());

        if (common_dates.empty()) continue;

        SpreadSeries ss;
        ss.name = instrument + "_spread_" + std::to_string(m_far) + "_" + std::to_string(m_near);
        ss.dates.reserve(common_dates.size());
        ss.values.reserve(common_dates.size());

        for (auto& d : common_dates) {
            ss.dates.push_back(d);
            ss.values.push_back(far_map[d] - near_map[d]);
        }

        spreads.push_back(std::move(ss));
    }
    return spreads;
}

double DataLoader::get_eurusd_rate(const std::string& date, db::Repository& repo) {
    return repo.get_eurusd_rate(date);
}

std::map<std::string, double>
DataLoader::build_eurusd_map(const std::vector<std::string>& dates, db::Repository& repo) {
    std::map<std::string, double> result;
    for (auto& d : dates) {
        result[d] = repo.get_eurusd_rate(d);
    }
    return result;
}

void DataLoader::convert_eur_to_usd(std::vector<SpreadSeries>& spreads,
                                     const std::map<std::string, double>& eurusd_map) {
    double default_rate = 1.10;
    for (auto& ss : spreads) {
        for (size_t i = 0; i < ss.values.size(); ++i) {
            double rate = default_rate;
            if (i < ss.dates.size()) {
                auto it = eurusd_map.find(ss.dates[i]);
                if (it != eurusd_map.end()) rate = it->second;
            }
            ss.values[i] *= rate;
        }
    }
}

} // namespace hf::core
