#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>
#include <list>
#include <map>
#include <vector>

namespace srtc::sctp
{

class DataChannelReceiveBuffer
{
public:
    struct Message {
        uint32_t ppid;
        ByteBuffer data;
    };

    // Records that 'ssn' was consumed by a control message (DCEP open/ack),
    // advancing mNextSsn without delivering a payload.
    void consumeSsn(uint16_t ssn);

    // Accepts one incoming DATA chunk fragment. 'flags' carries the B (0x01)
    // and E (0x02) bits from the DATA chunk header. Returns all messages now
    // deliverable: for unordered channels, any newly-completed message;
    // for ordered channels, all consecutive complete messages from mNextSsn.
    std::list<Message> receive(
        uint16_t ssn, uint8_t flags, bool unordered, uint32_t ppid, const uint8_t* data, size_t size);

private:
    struct PendingMessage {
        uint32_t ppid = 0;
        std::vector<uint8_t> payload;
        bool complete = false;
    };

    uint16_t mNextSsn = 0;
    std::map<uint16_t, PendingMessage> mPending;
};

} // namespace srtc::sctp
