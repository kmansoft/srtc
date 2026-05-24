#include "sctp/data_channel_receive_buffer.h"
#include "sctp/sctp_defs.h"

namespace srtc::sctp
{

void DataChannelReceiveBuffer::consumeSsn(uint16_t ssn)
{
    if (ssn == mNextSsn) {
        mNextSsn++;
    }
}

std::list<DataChannelReceiveBuffer::Message> DataChannelReceiveBuffer::receive(
    uint16_t ssn, uint8_t flags, bool unordered, uint32_t ppid, const uint8_t* data, size_t size)
{
    const bool isFirst = (flags & kDataFlagB) != 0;
    const bool isLast = (flags & kDataFlagE) != 0;

    std::list<Message> out;

    if (!unordered) {
        // Discard already-delivered SSNs (retransmits of fragments we already consumed).
        if (static_cast<int16_t>(ssn - mNextSsn) < 0)
            return out;
    }

    // Accumulate fragment into the pending entry for this SSN.
    auto& pending = mPending[ssn];
    if (isFirst) {
        pending.ppid = ppid;
        pending.payload.clear();
    }
    pending.payload.insert(pending.payload.end(), data, data + size);
    if (isLast) {
        pending.complete = true;
    }

    if (unordered) {
        if (pending.complete) {
            out.push_back({ pending.ppid, ByteBuffer(pending.payload.data(), pending.payload.size()) });
            mPending.erase(ssn);
        }
        return out;
    }

    // Ordered: drain consecutive complete messages from mNextSsn.
    while (true) {
        auto it = mPending.find(mNextSsn);
        if (it == mPending.end() || !it->second.complete)
            break;
        const auto& p = it->second;
        out.push_back({ p.ppid, ByteBuffer(p.payload.data(), p.payload.size()) });
        mPending.erase(it);
        mNextSsn++;
    }

    return out;
}

} // namespace srtc::sctp
