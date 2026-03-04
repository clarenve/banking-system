#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

inline uint64_t htonll(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(x & 0xFFFFFFFFULL)) << 32) |
           (uint64_t)htonl((uint32_t)(x >> 32));
#else
    return x;
#endif
}

inline uint64_t ntohll(uint64_t x){
    return htonll(x);
}

struct ByteWriter{
    std::vector<uint8_t> buffer;
    
    void u8(uint8_t value){ //1 byte
        buffer.push_back(value);
    }

    void u16(uint16_t value){
        uint16_t n = htons(value);
        auto p = reinterpret_cast<uint8_t*>(&n);
        buffer.insert(buffer.end(), p, p + sizeof(n));
    }

    void u32(uint32_t value){
        uint32_t n = htons(value);
        auto p = reinterpret_cast<uint8_t*>(&n);
        buffer.insert(buffer.end(), p, p + sizeof(n));
    }

    void bytes(const void* p, size_t n){
        const auto* b = reinterpret_cast<const uint8_t*>(p);
        buffer.insert(buffer.end(), b, b + n);
    }

    void str_with_len(const std::string& s){
        if(s.size() > 65535){
            throw std::runtime_error("string too long");
        }
        u16((uint16_t)s.size());
        bytes(s.data(), s.size());
    }
};

struct ByteReader{
    const uint8_t* p;
    size_t n;
    size_t i{0};

    ByteReader(const uint8_t* data, size_t len) : p(data), n(len) {}

    void need(size_t k){
        if (i + k > n) throw std::runtime_error("packet truncated");
    }

    uint8_t u8(){
        need(1);
        return p[i++];
    }

    uint16_t u16(){
        need(2);
        uint16_t v;
        std::memcpy(&v, p + i, 2);
        i += 2;
        return ntohs(v);
    }

    uint32_t u32(){
        need(4);
        uint32_t v;
        std::memcpy(&v, p + i, 4);
        i += 4;
        return ntohl(v);
    }

    uint64_t u64(){
        need(8);
        uint64_t v;
        std::memcpy(&v, p + i, 8);
        i += 8;
        return ntohll(v);
    }

    std::string str_u16len(){
        uint16_t len = u16();
        need(len);
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }

    void read_bytes(void* out, size_t len){
        need(len);
        std::memcpy(out, p + i, len);
        i += len;
    }
};