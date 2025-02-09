#include "srtc/rtp_packet_source.h"

#include <atomic>
#include <cstdlib>

namespace {

std::atomic<uint32_t> gNextUniqueId = 1;

}

namespace srtc {

RtpPacketSource::RtpPacketSource(uint32_t ssrc,
                                 uint8_t payloadId)
    : mSSRC(ssrc)
    , mPayloadId(payloadId)
    , mUniqueId(gNextUniqueId++)
    , mNextSequence(static_cast<uint16_t>(lrand48()))
{
}


uint32_t RtpPacketSource::getSSRC() const
{
    return mSSRC;
}

uint8_t RtpPacketSource::getPayloadId() const
{
    return mPayloadId;
}

uint32_t RtpPacketSource::getUniqueId() const
{
    return mUniqueId;
}

uint16_t RtpPacketSource::getNextSequence()
{
    return mNextSequence++;
}

}
