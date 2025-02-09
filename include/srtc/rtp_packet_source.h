#pragma once

#include <memory>
#include <cstdint>

namespace srtc {

class RtpPacketSource {
public:
    RtpPacketSource(uint32_t ssrc,
                    uint8_t payloadId);
    ~RtpPacketSource() = default;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;

    [[nodiscard]] uint16_t getNextSequence();

private:
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    uint16_t mNextSequence;
};

}

namespace std {

template<>
struct hash<std::shared_ptr<srtc::RtpPacketSource>> {
    std::size_t operator()(const std::shared_ptr<srtc::RtpPacketSource>& source) const
    {
        return std::hash<uint32_t>()(source->getSSRC() ^ source->getPayloadId());
    }
};

template<>
struct equal_to<std::shared_ptr<srtc::RtpPacketSource>> {
    bool operator()(const std::shared_ptr<srtc::RtpPacketSource>& lhs,
                    const std::shared_ptr<srtc::RtpPacketSource>& rhs) const
    {
        if (lhs.get() == rhs.get()) {
            return true;
        }

        return lhs->getSSRC() == rhs->getSSRC() && lhs->getPayloadId() == rhs->getPayloadId();
    }
};

}
