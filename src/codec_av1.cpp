#include "srtc/codec_av1.h"

namespace
{

uint64_t read_leb128(const uint8_t** data, size_t* size)
{
    uint64_t value = 0;
    int shift = 0;

    while (*size > 0 && shift < 56) {
        uint8_t byte = **data;
        (*data)++;
        (*size)--;

        value |= (static_cast<uint64_t>(byte & 0x7F)) << shift;

        if ((byte & 0x80) == 0)
            break; // MSB=0 means last byte
        shift += 7;
    }

    return value;
}

} // namespace

namespace srtc::av1
{

ObuParser::ObuParser(const ByteBuffer& buf)
    : mData(buf.data())
    , mEnd(buf.data() + buf.size())
    , mCurrType(0)
    , mCurrData(nullptr)
    , mCurrSize(0)
    , mCurrTemporalId(0)
    , mCurrSpatialId(0)
{
    parseImpl(mData, mEnd - mData);
}

ObuParser::operator bool() const
{
    return mCurrData && mCurrData < mEnd;
}

void ObuParser::next()
{
    if (mCurrData) {
        parseImpl(mCurrData + mCurrSize, mEnd - (mCurrData + mCurrSize));
    }
}

bool ObuParser::isAtEnd() const
{
    if (mCurrData == nullptr) {
        return true;
    }

    return mCurrData + mCurrSize >= mEnd;
}

uint8_t ObuParser::currType() const
{
    return mCurrType;
}

const uint8_t* ObuParser::currData() const
{
    return mCurrData;
}

size_t ObuParser::currSize() const
{
    return mCurrSize;
}

uint8_t ObuParser::currTemporalId() const
{
    return mCurrTemporalId;
}

uint8_t ObuParser::currSpatialId() const
{
    return mCurrSpatialId;
}

void ObuParser::parseImpl(const uint8_t* data, size_t size)
{
    // http://aomediacodec.github.io/av1-spec/#general-obu-syntax

    // Assume failure
    mCurrData = nullptr;
    mCurrType = 0;
    mCurrSize = 0;
    mCurrTemporalId = 0;
    mCurrSpatialId = 0;

    if (size < 1) {
        return;
    }

    // Header
    const uint8_t header = *data++;
    size -= 1;

    if ((header & 0x80) != 0) {
        return;
    }

    const uint8_t obu_type = (header >> 3) & 0x3F;
    const uint8_t has_extension = (header >> 2) & 0x01;
    const uint8_t has_size = (header >> 1) & 0x01;

    uint8_t temporal_id = 0;
    uint8_t spatial_id = 0;

    // Extension: temporal and spatial ids
    if (has_extension) {
        if (size < 1) {
            return;
        }
        uint8_t ext_header = *data++;
        size -= 1;

        temporal_id = (ext_header >> 5) & 0x07;
        spatial_id = (ext_header >> 3) & 0x03;
    }

    // Size: leb128
    size_t payload_size = size;
    if (has_size) {
        const auto leb128_size = read_leb128(&data, &size);
        if (leb128_size > payload_size) {
            return;
        }
        payload_size = leb128_size;
    }

    // Success
    mCurrData = data;
    mCurrSize = payload_size;
    mCurrType = obu_type;
    mCurrTemporalId = temporal_id;
    mCurrSpatialId = spatial_id;
}

//////////

static constexpr uint8_t AV1_KEY_FRAME = 0;
// static constexpr uint8_t AV1_INTER_FRAME = 1;
static constexpr uint8_t AV1_INTRA_ONLY_FRAME = 2;
// static constexpr uint8_t AV1_SWITCH_FRAME = 3;

bool isFrameObuType(uint8_t obuType)
{
    return obuType == ObuType::FrameHeader || obuType == ObuType::Frame || obuType == ObuType::RedundantFrame;
}

bool isKeyFrameObu(uint8_t obuType, const uint8_t* data, size_t size)
{
    if (isFrameObuType(obuType) && size > 1) {
        // https://aomediacodec.github.io/av1-spec/#frame-header-obu-syntax
        // https://aomediacodec.github.io/av1-spec/#frame-obu-syntax
        // https://aomediacodec.github.io/av1-spec/#uncompressed-header-syntax
        const auto header = data[0];
        if ((header & 0x80) != 0) {
            // Show existing frame
            return false;
        }

        // https://aomediacodec.github.io/av1-spec/#uncompressed-header-semantics
        const auto frame_type = (header >> 5) & 0x03;
        return frame_type == AV1_KEY_FRAME || frame_type == AV1_INTRA_ONLY_FRAME;
    }

    return false;
}

} // namespace srtc::av1

