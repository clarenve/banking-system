#pragma once
#include <cstdint>

static constexpr uint8_t version_number = 1;

enum class Opcode : uint8_t{
    OPEN_ACCOUNT = 1,

};

enum class Semantics : uint8_t{
    AT_LEAST_ONCE = 0,
    AT_MOST_ONCE  = 1,
};

enum class Status : uint8_t{
    SUCCESS = 0,
    ERROR = 1,
};

enum class Currency : uint8_t{
    SGD = 0,
    RM = 1,
};