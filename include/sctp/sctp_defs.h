#pragma once

#include <cstddef>
#include <cstdint>

namespace srtc::sctp {

const char* formatChunkName(uint8_t type);
const char* formatParamName(uint16_t type);

// Constant-time byte comparison — always visits every byte to prevent timing side-channels
bool constTimeEqual(const uint8_t* a, const uint8_t* b, size_t size);

// SCTP chunk types (RFC 4960)
constexpr uint8_t kChunkData             = 0;
constexpr uint8_t kChunkInit             = 1;
constexpr uint8_t kChunkInitAck          = 2;
constexpr uint8_t kChunkSack             = 3;
constexpr uint8_t kChunkHeartbeat        = 4;
constexpr uint8_t kChunkHeartbeatAck     = 5;
constexpr uint8_t kChunkAbort            = 6;
constexpr uint8_t kChunkShutdown         = 7;
constexpr uint8_t kChunkShutdownAck      = 8;
constexpr uint8_t kChunkError            = 9;
constexpr uint8_t kChunkCookieEcho       = 10;
constexpr uint8_t kChunkCookieAck        = 11;
constexpr uint8_t kChunkShutdownComplete = 14;
constexpr uint8_t kChunkForwardTsn       = 0xC0;
constexpr uint8_t kChunkReconfig         = 0x82;

// DATA chunk flags (RFC 4960 §3.3.1)
constexpr uint8_t kDataFlagComplete  = 0x03; // B + E: single-fragment message
constexpr uint8_t kDataFlagUnordered = 0x04; // U: unordered delivery

// PPID values (IANA registry)
constexpr uint32_t kPpidDcep          = 50; // WebRTC DCEP (RFC 8832)
constexpr uint32_t kPpidString        = 51; // WebRTC string
constexpr uint32_t kPpidBinary        = 53; // WebRTC binary
constexpr uint32_t kPpidStringEmpty   = 56; // WebRTC string, zero length
constexpr uint32_t kPpidBinaryEmpty   = 57; // WebRTC binary, zero length

// DCEP message types (RFC 8832)
constexpr uint8_t kDcepMsgAck  = 0x02;
constexpr uint8_t kDcepMsgOpen = 0x03;

// DCEP channel types
constexpr uint8_t  kDcepChannelReliable = 0x00;
constexpr uint16_t kDcepPriorityNormal  = 256;

// SCTP parameter types
constexpr uint16_t kParamStateCookie          = 7;
constexpr uint16_t kParamSupportedExtensions  = 0x8008;
constexpr uint16_t kParamForwardTsnSupported  = 0xC000;

// Session limits
constexpr uint32_t kInitRwnd    = 131072;   // 128 KB receive window
constexpr uint16_t kInitStreams = 1024;

// Retransmit timers (RFC 4960 defaults)
constexpr unsigned kMaxInitRetransmits = 8;      // max retransmits for INIT and COOKIE_ECHO
constexpr uint32_t kRtoInitialMs       = 3000;   // initial RTO in milliseconds
constexpr uint32_t kRtoMaxMs           = 60000;  // maximum RTO in milliseconds

// State Cookie layout: kCookieDataSize bytes of fields, then kCookieHmacSize bytes of HMAC-SHA1
constexpr uint32_t kCookieLifetime  = 60;   // seconds
constexpr size_t   kCookieDataSize  = 36;
constexpr size_t   kCookieHmacSize  = 20;
constexpr size_t   kCookieTotalSize = kCookieDataSize + kCookieHmacSize;

} // namespace srtc::sctp
