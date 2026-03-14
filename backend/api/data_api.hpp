#pragma once
#include <httplib.h>
#include <string>
#include "../database/repository.hpp"

namespace hf::api {

void register_data_routes(httplib::Server& svr,
                           db::Repository& repo,
                           const std::string& jwt_secret);

} // namespace hf::api
