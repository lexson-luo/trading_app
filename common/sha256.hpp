#pragma once
// Public domain SHA-256. No external dependencies.
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

namespace hf::crypto {

namespace detail {
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32-n)); }
inline uint32_t Ch(uint32_t x,uint32_t y,uint32_t z){return (x&y)^(~x&z);}
inline uint32_t Maj(uint32_t x,uint32_t y,uint32_t z){return (x&y)^(x&z)^(y&z);}
inline uint32_t S0(uint32_t x){return rotr(x,2)^rotr(x,13)^rotr(x,22);}
inline uint32_t S1(uint32_t x){return rotr(x,6)^rotr(x,11)^rotr(x,25);}
inline uint32_t s0(uint32_t x){return rotr(x,7)^rotr(x,18)^(x>>3);}
inline uint32_t s1(uint32_t x){return rotr(x,17)^rotr(x,19)^(x>>10);}
} // namespace detail

struct SHA256Context {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
};

inline void sha256_init(SHA256Context& ctx) {
    ctx.state[0]=0x6a09e667; ctx.state[1]=0xbb67ae85;
    ctx.state[2]=0x3c6ef372; ctx.state[3]=0xa54ff53a;
    ctx.state[4]=0x510e527f; ctx.state[5]=0x9b05688c;
    ctx.state[6]=0x1f83d9ab; ctx.state[7]=0x5be0cd19;
    ctx.count = 0;
}

inline void sha256_transform(SHA256Context& ctx, const uint8_t* data) {
    using namespace detail;
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for(int i=0;i<16;i++)
        w[i]=(uint32_t)data[i*4]<<24|(uint32_t)data[i*4+1]<<16|(uint32_t)data[i*4+2]<<8|(uint32_t)data[i*4+3];
    for(int i=16;i<64;i++) w[i]=s1(w[i-2])+w[i-7]+s0(w[i-15])+w[i-16];
    a=ctx.state[0];b=ctx.state[1];c=ctx.state[2];d=ctx.state[3];
    e=ctx.state[4];f=ctx.state[5];g=ctx.state[6];h=ctx.state[7];
    for(int i=0;i<64;i++){
        t1=h+S1(e)+Ch(e,f,g)+K[i]+w[i];
        t2=S0(a)+Maj(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx.state[0]+=a;ctx.state[1]+=b;ctx.state[2]+=c;ctx.state[3]+=d;
    ctx.state[4]+=e;ctx.state[5]+=f;ctx.state[6]+=g;ctx.state[7]+=h;
}

inline void sha256_update(SHA256Context& ctx, const uint8_t* data, size_t len) {
    size_t idx = ctx.count % 64;
    ctx.count += len;
    size_t rem = 64 - idx;
    size_t i = 0;
    if (len >= rem) {
        memcpy(ctx.buf + idx, data, rem);
        sha256_transform(ctx, ctx.buf);
        for (i = rem; i + 63 < len; i += 64)
            sha256_transform(ctx, data + i);
        idx = 0;
    }
    memcpy(ctx.buf + idx, data + i, len - i);
}

inline void sha256_final(SHA256Context& ctx, uint8_t digest[32]) {
    uint8_t bits[8];
    uint64_t bc = ctx.count * 8;
    for(int i=7;i>=0;i--){ bits[i]=(uint8_t)(bc&0xff); bc>>=8; }
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    while (ctx.count % 64 != 56) { uint8_t z=0; sha256_update(ctx, &z, 1); }
    sha256_update(ctx, bits, 8);
    for(int i=0;i<8;i++){
        digest[i*4]  =(ctx.state[i]>>24)&0xff;
        digest[i*4+1]=(ctx.state[i]>>16)&0xff;
        digest[i*4+2]=(ctx.state[i]>>8)&0xff;
        digest[i*4+3]= ctx.state[i]&0xff;
    }
}

inline std::string sha256_hex(const std::string& input) {
    SHA256Context ctx;
    sha256_init(ctx);
    sha256_update(ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t digest[32];
    sha256_final(ctx, digest);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for(int i=0;i<32;i++) oss << std::setw(2) << (unsigned)digest[i];
    return oss.str();
}

// HMAC-SHA256 for JWT signing
inline std::string hmac_sha256(const std::string& key, const std::string& msg) {
    uint8_t k_ipad[64]={}, k_opad[64]={};
    const auto* k = reinterpret_cast<const uint8_t*>(key.data());
    if (key.size() > 64) {
        SHA256Context c; sha256_init(c);
        sha256_update(c, k, key.size());
        uint8_t d[32]; sha256_final(c, d);
        memcpy(k_ipad, d, 32); memcpy(k_opad, d, 32);
    } else {
        memcpy(k_ipad, k, key.size()); memcpy(k_opad, k, key.size());
    }
    for(int i=0;i<64;i++){ k_ipad[i]^=0x36; k_opad[i]^=0x5c; }
    // inner
    SHA256Context inner; sha256_init(inner);
    sha256_update(inner, k_ipad, 64);
    sha256_update(inner, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    uint8_t inner_d[32]; sha256_final(inner, inner_d);
    // outer
    SHA256Context outer; sha256_init(outer);
    sha256_update(outer, k_opad, 64);
    sha256_update(outer, inner_d, 32);
    uint8_t out[32]; sha256_final(outer, out);
    // return as hex
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for(int i=0;i<32;i++) oss << std::setw(2) << (unsigned)out[i];
    return oss.str();
}

// Simple password hash: sha256(salt + ":" + password)
inline std::string hash_password(const std::string& password, const std::string& salt) {
    return sha256_hex(salt + ":" + password);
}

} // namespace hf::crypto
