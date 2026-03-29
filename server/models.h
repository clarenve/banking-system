#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <netinet/in.h>

#include "../common/protocol.h"

struct Account{
    uint32_t aid;
    std::string name;
    std::string password;
    Currency currency;
    double balance;
};

struct ReqID{
    uint32_t rid;
    uint32_t ip;
    uint16_t port;

    bool operator==(const ReqID& o) const{
        return rid == o.rid && ip == o.ip && port == o.port;
    }
};

struct ReqIDHash{
    size_t operator()(const ReqID& k) const{
        return (size_t)k.rid ^ ((size_t)k.ip << 1) ^ ((size_t)k.port << 16);
    }
};

struct MonitorClient{
    sockaddr_in addr{};
    std::chrono::steady_clock::time_point expiry;
};