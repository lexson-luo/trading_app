#pragma once
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace hf::utils {

inline std::string generate_token(size_t bytes = 16) {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes; ++i)
        oss << std::setw(2) << dist(rng);
    return oss.str();
}

inline std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

} // namespace hf::utils
