#include "sctp/sctp_defs.h"

namespace srtc::sctp {

const char* formatChunkName(uint8_t type)
{
    switch (type) {
    case kChunkData:             return "DATA";
    case kChunkInit:             return "INIT";
    case kChunkInitAck:          return "INIT_ACK";
    case kChunkSack:             return "SACK";
    case kChunkHeartbeat:        return "HEARTBEAT";
    case kChunkHeartbeatAck:     return "HEARTBEAT_ACK";
    case kChunkAbort:            return "ABORT";
    case kChunkShutdown:         return "SHUTDOWN";
    case kChunkShutdownAck:      return "SHUTDOWN_ACK";
    case kChunkError:            return "ERROR";
    case kChunkCookieEcho:       return "COOKIE_ECHO";
    case kChunkCookieAck:        return "COOKIE_ACK";
    case kChunkShutdownComplete: return "SHUTDOWN_COMPLETE";
    case kChunkForwardTsn:       return "FORWARD_TSN";
    case kChunkReconfig:         return "RECONFIG";
    default:                     return "UNKNOWN";
    }
}

const char* formatParamName(uint16_t type)
{
    switch (type) {
    case kParamStateCookie:         return "StateCookie";
    case kParamSupportedExtensions: return "SupportedExtensions";
    case kParamForwardTsnSupported: return "ForwardTsnSupported";
    default:                        return "Unknown";
    }
}

bool constTimeEqual(const uint8_t* a, const uint8_t* b, size_t size)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < size; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

} // namespace srtc::sctp
