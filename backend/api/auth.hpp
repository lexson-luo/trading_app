#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include "jwt.hpp"
#include "../database/repository.hpp"

namespace hf::api {

// Extract and verify Bearer token from Authorization header.
// Throws std::runtime_error with HTTP status string if invalid.
// Returns verified Claims on success.
jwt::Claims require_auth(const httplib::Request& req,
                          const std::string& jwt_secret);

// Require admin role; throws 403 if not admin.
void require_admin(const jwt::Claims& claims);

// Setup auth routes on the server
void register_auth_routes(httplib::Server&      svr,
                           db::Repository&       repo,
                           const std::string&    jwt_secret);

} // namespace hf::api
