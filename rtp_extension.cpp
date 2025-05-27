#include "srtc/rtp_extension.h"

#include <memory>

namespace srtc
{

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
	if (this != &source) {
		mId = source.mId;
		mData = std::move(source.mData);

		source.mId = 0;
	}

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

size_t RtpExtension::size() const
{
    if (empty()) {
        return 0;
    }

    return 2 /* extension id */ + 2 /* extension length */ + 4 * ((mData.size() + 3) / 4);
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
    return { mId, mData.copy() };
}

} // namespace srtc
