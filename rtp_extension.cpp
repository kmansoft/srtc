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
}

RtpExtension& RtpExtension::operator=(RtpExtension&& source) noexcept
{
    mId = source.mId;
    mData = std::move(source.mData);
    return *this;
}

uint16_t RtpExtension::getId() const
{
    return mId;
}

const ByteBuffer& RtpExtension::getData() const
{
    return mData;
}


}