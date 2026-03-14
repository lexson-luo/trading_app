#pragma once
// Minimal HS256 JWT implementation (no external dependencies)
#include <string>
#include <sstream>
#include <chrono>
#include <nlohmann/json.hpp>
#include "sha256.hpp"

namespace hf::jwt {

// ── Base64url (no padding) ─────────────────────────────────────────────────
inline std::string b64url_encode(const std::string& in) {
    static const char* tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned b0 = (uint8_t)in[i];
        unsigned b1 = i+1 < in.size() ? (uint8_t)in[i+1] : 0;
        unsigned b2 = i+2 < in.size() ? (uint8_t)in[i+2] : 0;
        out += tab[(b0 >> 2) & 0x3f];
        out += tab[((b0 << 4)|(b1 >> 4)) & 0x3f];
        out += i+1 < in.size() ? tab[((b1 << 2)|(b2 >> 6)) & 0x3f] : '=';
        out += i+2 < in.size() ? tab[b2 & 0x3f] : '=';
    }
    // replace + → - , / → _ , strip =
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

inline std::string b64url_decode(const std::string& in) {
    std::string s = in;
    for (auto& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4) s += '=';
    std::string out;
    static const int8_t lookup[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    for (size_t i = 0; i < s.size(); i += 4) {
        int a = lookup[(uint8_t)s[i]], b = lookup[(uint8_t)s[i+1]];
        int c = lookup[(uint8_t)s[i+2]], d = lookup[(uint8_t)s[i+3]];
        if (a<0||b<0) break;
        out += (char)((a<<2)|(b>>4));
        if (c>=0) out += (char)(((b&0xf)<<4)|(c>>2));
        if (d>=0) out += (char)(((c&0x3)<<6)|d);
    }
    return out;
}

// ── Token creation / verification ──────────────────────────────────────────
inline std::string create_token(const std::string& secret,
                                 const std::string& username,
                                 const std::string& role,
                                 int64_t ttl_seconds = 28800) {
    using namespace std::chrono;
    int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

    std::string header  = b64url_encode(R"({"alg":"HS256","typ":"JWT"})");
    nlohmann::json payload_j = {
        {"sub", username}, {"role", role},
        {"iat", now}, {"exp", now + ttl_seconds}
    };
    std::string payload = b64url_encode(payload_j.dump());
    std::string signing_input = header + "." + payload;
    // HMAC returns hex; encode that as b64url for signature
    std::string sig_hex = crypto::hmac_sha256(secret, signing_input);
    // convert hex → bytes → b64url
    std::string sig_bytes;
    for (size_t i = 0; i < sig_hex.size(); i += 2) {
        sig_bytes += (char)std::stoi(sig_hex.substr(i,2), nullptr, 16);
    }
    return signing_input + "." + b64url_encode(sig_bytes);
}

struct Claims {
    bool        valid{false};
    std::string username;
    std::string role;
    int64_t     exp{0};
    std::string error;
};

inline Claims verify_token(const std::string& secret, const std::string& token) {
    Claims c;
    auto dot1 = token.find('.');
    auto dot2 = token.find('.', dot1+1);
    if (dot1 == std::string::npos || dot2 == std::string::npos) {
        c.error = "malformed"; return c;
    }
    std::string signing_input = token.substr(0, dot2);
    std::string sig_b64 = token.substr(dot2+1);

    // verify signature
    std::string expected_hex = crypto::hmac_sha256(secret, signing_input);
    std::string expected_bytes;
    for (size_t i = 0; i < expected_hex.size(); i += 2)
        expected_bytes += (char)std::stoi(expected_hex.substr(i,2), nullptr, 16);
    if (b64url_encode(expected_bytes) != sig_b64) {
        c.error = "invalid signature"; return c;
    }

    // decode payload
    try {
        auto payload_str = b64url_decode(token.substr(dot1+1, dot2-dot1-1));
        auto j = nlohmann::json::parse(payload_str);
        using namespace std::chrono;
        int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        c.exp = j.at("exp").get<int64_t>();
        if (c.exp < now) { c.error = "expired"; return c; }
        c.username = j.at("sub").get<std::string>();
        c.role     = j.at("role").get<std::string>();
        c.valid    = true;
    } catch (...) { c.error = "parse error"; }
    return c;
}

} // namespace hf::jwt
