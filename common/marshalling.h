/*
marshalling.h provides helper utilities for encoding and decoding messages
- ByteWriter is used to serialize data into a byte buffer
- ByteReader is used to parse data from received packets
- all values are stored in network byte order
- supports fixed-size types and length-prefixed strings
*/

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <arpa/inet.h>

//convert 64-bit integer to network byte order
//handles little endian systems manually
inline uint64_t hton_u64(uint64_t x){
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(x & 0xFFFFFFFFULL)) << 32) |
           (uint64_t)htonl((uint32_t)(x >> 32));
#else
    return x;
#endif
}

//convert 64-bit integer from network byte order to host order
inline uint64_t ntoh_u64(uint64_t x){
    return hton_u64(x);
}

//convert raw uint64_t bits into double
inline double u64_to_double(uint64_t u){
    double x;
    std::memcpy(&x, &u, sizeof(x));
    return x;
}

//convert double into raw uint64_t bits
inline uint64_t double_to_u64(double x){
    uint64_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

/*
ByteWriter builds a byte buffer for sending requests/replies
- appends values in network byte order
- used to marshal data before sending over udp
*/
struct ByteWriter{
    std::vector<uint8_t> buffer;

    //append 1 byte
    void u8(uint8_t value){
        buffer.push_back(value);
    }

    //append 2 bytes
    void u16(uint16_t value){
        uint16_t n = htons(value);
        auto p = reinterpret_cast<uint8_t*>(&n);
        buffer.insert(buffer.end(), p, p + sizeof(n));
    }

    //append 4 bytes
    void u32(uint32_t value){
        uint32_t n = htonl(value);
        auto p = reinterpret_cast<uint8_t*>(&n);
        buffer.insert(buffer.end(), p, p + sizeof(n));
    }

    //append 8 bytes
    void u64(uint64_t value){
        uint64_t n = hton_u64(value);
        auto p = reinterpret_cast<uint8_t*>(&n);
        buffer.insert(buffer.end(), p, p + sizeof(n));
    }

    //append n bytes into buffer
    void bytes(const void* p, size_t n){
        const auto* b = reinterpret_cast<const uint8_t*>(p);
        buffer.insert(buffer.end(), b, b + n);
    }

    //append string with 16-bit length prefix
    //format: [length][string bytes]
    void str_with_len(const std::string& s){
        if(s.size() > UINT16_MAX){
            throw std::runtime_error("string too long");
        }
        u16((uint16_t)s.size());
        bytes(s.data(), s.size());
    }
};

/*
ByteReader parses incoming byte buffers
- reads values in the same order they were written
- ensures bounds checking to prevent invalid reads
*/
struct ByteReader{
    const uint8_t* p;
    size_t n;
    size_t i{0};

    //initialize reader with data buffer and length
    ByteReader(const uint8_t* data, size_t len) : p(data), n(len) {}

    //ensure there are at least k bytes remaining
    //throws error if packet is truncated
    void need(size_t k){
        if (i + k > n) throw std::runtime_error("packet truncated");
    }

    //read 1 byte
    uint8_t u8(){
        need(1);
        return p[i++];
    }

    //read 2 bytes and convert from network byte order
    uint16_t u16(){
        need(2);
        uint16_t v;
        std::memcpy(&v, p + i, 2);
        i += 2;
        return ntohs(v);
    }

    //read 4 bytes and convert from network byte order
    uint32_t u32(){
        need(4);
        uint32_t v;
        std::memcpy(&v, p + i, 4);
        i += 4;
        return ntohl(v);
    }

    //read 8 bytes and convert from network byte order
    uint64_t u64(){
        need(8);
        uint64_t v;
        std::memcpy(&v, p + i, 8);
        i += 8;
        return ntoh_u64(v);
    }

    //read length-prefixed string (uint16_t length)
    std::string str_u16len(){
        uint16_t len = u16();
        need(len);
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }

    //read raw bytes of size len
    void read_bytes(void* out, size_t len){
        need(len);
        std::memcpy(out, p + i, len);
        i += len;
    }
};