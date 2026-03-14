#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>
#include "types.hpp"
#include "../database/repository.hpp"

namespace hf::core {

class DataLoader {
public:
    // Load prices from DB, return: contract_month → [(date, price)]
    // Prices are sorted by date ASC within each contract month
    static std::map<int, std::vector<std::pair<std::string, double>>>
    load_prices(const std::string& instrument,
                const std::string& start_date,
                const std::string& end_date,
                db::Repository& repo);

    // Compute term spreads: spread_n+1_n = M(n+1) - M(n) for n=1..11
    // Aligns on common dates across both contract months
    static std::vector<SpreadSeries>
    compute_spreads(const std::map<int, std::vector<std::pair<std::string, double>>>& prices,
                    const std::string& instrument);

    // Returns EUR/USD rate for a given date (from DB or default 1.10)
    static double get_eurusd_rate(const std::string& date, db::Repository& repo);

    // Build a date → eurusd map for a set of dates
    static std::map<std::string, double>
    build_eurusd_map(const std::vector<std::string>& dates, db::Repository& repo);

    // Convert EEX SMP spreads from EUR to USD in-place (multiply by eurusd rate)
    static void convert_eur_to_usd(std::vector<SpreadSeries>& spreads,
                                    const std::map<std::string, double>& eurusd_map);
};

} // namespace hf::core
