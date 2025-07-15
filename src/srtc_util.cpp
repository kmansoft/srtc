#include "srtc/srtc_util.h"

#include <cstring>

namespace srtc
{

size_t compressNackList(const std::vector<uint16_t>& nackList, uint16_t* buf_seq, uint16_t* buf_blp)
{
    const size_t in_end = nackList.size();

    std::memset(buf_seq, 0, sizeof(uint16_t) * in_end);
    std::memset(buf_blp, 0, sizeof(uint16_t) * in_end);

    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < in_end) {
        const auto seq = nackList[in_pos];

        buf_seq[out_pos] = seq;
        buf_blp[out_pos] = 0;

        in_pos += 1;

        while (in_pos < in_end && static_cast<uint16_t>(nackList[in_pos] - seq) <= 16) {
            buf_blp[out_pos] |= 1 << (static_cast<uint16_t>(nackList[in_pos] - seq - 1));
            in_pos += 1;
        }

        out_pos += 1;
    }

    return out_pos;
}

} // namespace srtc