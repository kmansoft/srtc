#include "srtc/rtp_packet_source.h"

#include <atomic>
#include <cstdlib>

namespace srtc {

RtpPacketSource::RtpPacketSource(uint32_t ssrc,
                                 uint8_t payloadId)
    : mSSRC(ssrc)
    , mPayloadId(payloadId)
    , mGeneratedCount(0)
    , mRollover(0)
    , mNextSequence(10000 + static_cast<uint16_t>(lrand48() % 20000))
{
}

RtpPacketSource::~RtpPacketSource() = default;

uint32_t RtpPacketSource::getSSRC() const
{
    return mSSRC;
}

uint8_t RtpPacketSource::getPayloadId() const
{
    return mPayloadId;
}

std::pair<uint32_t, uint16_t> RtpPacketSource::getNextSequence()
{
    mGeneratedCount += 1;
    mNextSequence += 1;

    if (mGeneratedCount > 1 && mNextSequence == 0) {
        mRollover += 1;
    }

    return { mRollover, mNextSequence };
}

}
