#include "srtc/rtp_packet_source.h"

#include <atomic>
#include <cstdlib>

namespace {

std::atomic<uint32_t> gNextUniqueId = 1;

}

namespace srtc {

RtpPacketSource::RtpPacketSource()
    : mUniqueId(gNextUniqueId++)
    , mNextSequence(static_cast<uint16_t>(lrand48()))
{
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
