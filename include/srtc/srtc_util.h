#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace srtc
{

size_t compressNackList(const std::vector<uint16_t>& nackList, uint16_t* buf_seq, uint16_t* buf_blp);

}
