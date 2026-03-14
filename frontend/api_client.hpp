#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <mutex>

namespace hf::client {

struct ApiResponse {
    bool           ok{false};
    int            status{0};
    nlohmann::json body;
    std::string    error;
};

class ApiClient {
public:
    explicit ApiClient(const std::string& host = "localhost", int port = 8080);

    void set_token(const std::string& token);
    void clear_token();
    bool has_token() const;
    const std::string& host() const { return host_; }
    int                port() const { return port_; }

    ApiResponse post(const std::string& path, const nlohmann::json& body);
    ApiResponse get(const std::string& path);
    ApiResponse del(const std::string& path);

private:
    std::string        host_;
    int                port_;
    std::string        token_;
    mutable std::mutex mu_;

    httplib::Headers auth_headers() const;
};

} // namespace hf::client
