/*
reply_utils.h provides helper functions for building reply messages
- constructs standard reply header (status + rid)
- used by server handlers to create responses
*/

#pragma once
#include <cstdint>

#include "../common/protocol.h"
#include "../common/marshalling.h"

ByteWriter build_reply_header(Status status, uint32_t rid);