#include "reply_utils.h"

ByteWriter build_reply_header(Status status, uint32_t rid){
    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(status));
    bw.u16(0);
    bw.u32(rid);
    return bw;
}