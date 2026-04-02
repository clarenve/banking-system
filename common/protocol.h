/*
protocol.h defines all shared protocol constants and enums
- used by both client and server
- ensures both sides interpret messages consistently
*/

#pragma once
#include <cstdint>

//protocol version used for compatibility checks
static constexpr uint8_t version_number = 1;

//supported request types sent from client to server
enum class Opcode : uint8_t{
    OPEN_ACCOUNT = 1,
    CLOSE_ACCOUNT = 2,
    DEPOSIT = 3,
    WITHDRAW = 4,
    MONITOR = 5,
    VIEW_ACCOUNT = 6,
    TRANSFER = 7,
};

//invocation semantics used for request handling
enum class Semantics : uint8_t{
    AT_LEAST_ONCE = 0,
    AT_MOST_ONCE  = 1,
};

//status of server reply
enum class Status : uint8_t{
    SUCCESS = 0,
    ERROR = 1,
};

//supported account currencies
enum class Currency : uint8_t{
    SGD = 0,
    RM = 1,
};