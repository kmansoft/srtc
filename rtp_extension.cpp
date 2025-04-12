#include "srtc/rtp_extension.h"

#include <memory>

namespace srtc {

RtpExtension::RtpExtension()
    : mId(0)
{
}

RtpExtension::RtpExtension(uint16_t id, ByteBuffer&& data)
    : mId(id)
    , mData(std::move(data))
{
}

RtpExtension::RtpExtension(RtpExtension&& source) noexcept
    : mId(source.mId)
    , mData(std::move(source.mData))
{
    source.mId = 0;
}

RtpExtension& RtpExtension::operator=(RtpExtension&& source) noexcept
{
    mId = source.mId;
    mData = std::move(source.mData);

    source.mId = 0;

    return *this;
}

void RtpExtension::clear()
{
    mId = 0;
    mData.clear();
}

bool RtpExtension::empty() const
{
    return mId == 0 || mData.empty();
}

uint16_t RtpExtension::getId() const
{
    return mId;
}

const ByteBuffer& RtpExtension::getData() const
{
    return mData;
}

RtpExtension RtpExtension::copy() const
{
    return {
        mId, mData.copy()
    };
}

}
