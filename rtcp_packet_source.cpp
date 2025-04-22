#include "srtc/rtcp_packet_source.h"

namespace srtc {

RtcpPacketSource::RtcpPacketSource(uint32_t ssrc)
    : mSSRC(ssrc)
    , mNextSequence(0)
{
}

RtcpPacketSource::~RtcpPacketSource() = default;

uint32_t RtcpPacketSource::getSSRC() const
{
    return mSSRC;
}

uint32_t RtcpPacketSource::getNextSequence()
{
    mNextSequence += 1;
    return mNextSequence;
}

void RtcpPacketSource::clear()
{
    mNextSequence = 0;
}

}