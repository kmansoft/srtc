#include "srtc/rtcp_packet_multi.h"
#include "srtc/byte_buffer.h"
#include "srtc/rtcp_packet.h"

namespace
{

srtc::ByteBuffer combine(const std::vector<std::shared_ptr<srtc::RtcpPacket>>& packetList)
{
    srtc::ByteBuffer buf;
    srtc::ByteWriter w(buf);

    for (const auto& p : packetList) {
        w.write(p->generate());
    }

    return buf;
}

} // namespace

namespace srtc
{

RtcpPacketMulti::RtcpPacketMulti(const std::vector<std::shared_ptr<RtcpPacket>>& packetList)
    : mMulti(combine(packetList))
{
}

RtcpPacketMulti::~RtcpPacketMulti() = default;

ByteBuffer RtcpPacketMulti::generate() const
{
    return mMulti.copy();
}

}; // namespace srtc
