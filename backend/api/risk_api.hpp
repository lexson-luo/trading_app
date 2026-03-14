#pragma once
#include <httplib.h>
#include <string>
#include "../database/repository.hpp"
#include "../core/live_trader.hpp"

namespace hf::api {

void register_risk_routes(httplib::Server&     svr,
                           db::Repository&      repo,
                           core::LiveTrader&    live_trader,
                           const std::string&   jwt_secret);

} // namespace hf::api
