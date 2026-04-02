/*
models.h defines core data structures used by the server
- Account stores account information (name, password, balance, currency)
- ReqID uniquely identifies a client request using rid + ip + port
- ReqIDHash is used for hashing ReqID in unordered_map
*/

#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <netinet/in.h>

#include "../common/protocol.h"

//represents a bank account stored on the server
struct Account{
    uint32_t aid;
    std::string name;
    std::string password;
    Currency currency;
    double balance;
};

//unique identifier for a request (used for duplicate detection)
struct ReqID{
    uint32_t rid;
    uint32_t ip;
    uint16_t port;

    bool operator==(const ReqID& o) const{
        return rid == o.rid && ip == o.ip && port == o.port;
    }
};

//hash function for ReqID to be used in unordered_map
struct ReqIDHash{
    size_t operator()(const ReqID& k) const{
        return (size_t)k.rid ^ ((size_t)k.ip << 1) ^ ((size_t)k.port << 16);
    }
};

//represents a client subscribed to receive monitor updates
//stores client address and expiry time for the subscription
struct MonitorClient{
    sockaddr_in addr{};
    std::chrono::steady_clock::time_point expiry;
};