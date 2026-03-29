#pragma once

#include <cstdint>

#include "../common/protocol.h"
#include "../common/marshalling.h"

ByteWriter build_reply_header(Status status, uint32_t rid);