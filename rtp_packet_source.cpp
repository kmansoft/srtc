#include "srtc/rtp_packet_source.h"

#include <atomic>
#include <cstdlib>

namespace srtc {

RtpPacketSource::RtpPacketSource(uint32_t ssrc,
                                 uint8_t payloadId)
    : mSSRC(ssrc)
    , mPayloadId(payloadId)
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

uint16_t RtpPacketSource::getNextSequence()
{
    return mNextSequence++;
}

}
