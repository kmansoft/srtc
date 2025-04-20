#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"

#include <memory>
#include <cstddef>
#include <cstdint>

namespace srtc {

class Track;

class RtpPacket {
public:
    // https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
    // https://webrtc.googlesource.com/src/+/refs/heads/main/media/base/media_constants.cc#17
    static constexpr size_t kMaxPayloadSize = 1200;

    RtpPacket(const std::shared_ptr<Track>& track,
              bool marker,
              uint32_t rollover,
              uint16_t sequence,
              uint32_t timestamp,
              ByteBuffer&& payload);

    RtpPacket(const std::shared_ptr<Track>& track,
              bool marker,
              uint32_t rollover,
              uint16_t sequence,
              uint32_t timestamp,
              RtpExtension&& extension,
              ByteBuffer&& payload);

    ~RtpPacket();

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;
    [[nodiscard]] RtpExtension getExtension() const;
    [[nodiscard]] uint8_t getPayloadId() const;
    [[nodiscard]] uint32_t getRollover() const;
    [[nodiscard]] uint16_t getSequence() const;
    [[nodiscard]] uint32_t getSSRC() const;

    struct Output {
        ByteBuffer buf;
        uint32_t rollover;
    };

    [[nodiscard]] Output generate() const;
    [[nodiscard]] Output generateRtx(const RtpExtension& extension) const;

private:
    const std::shared_ptr<Track> mTrack;
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    const bool mMarker;
    const uint32_t mRollover;
    const uint16_t mSequence;
    const uint32_t mTimestamp;
    const RtpExtension mExtension;
    const ByteBuffer mPayload;
};

}
